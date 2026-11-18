#!/usr/bin/env python3
"""Port census: try every TortoiseBots .cpp against ForeverCore and count what does not compile, and why.

    python3 forever/tools/port_census.py <core build dir> <out dir> [jobs] [only-substring]

Run it where the core was configured (the build VM, inside the builder image). It takes the compiler line of the
module's own TbScripts.cpp from `ninja -t compdb`, swaps the source file, adds forever/compat and the old include
folders, and runs syntax-only with no error limit. Nothing is linked and no file is changed.

Output in <out dir>: files.tsv (errors seen when compiling each .cpp), where.tsv (error places per file that holds
them, each place counted once), kinds.tsv (by message, names removed), names.tsv (identifiers named most),
fatal.tsv (headers ForeverCore does not have), clean.txt, summary.txt.
"""
import collections, concurrent.futures, json, os, re, shlex, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OLD_INCLUDE_DIRS = ["ai", "ai/playerbot", "ai/playerbot/strategy", "ai/playerbot/strategy/actions",
                    "ai/playerbot/strategy/triggers", "ai/playerbot/strategy/values", "host", "runtime"]
SKIP_DIRS = (".git", "forever", "tools")


def base_command(build):
    db = json.loads(subprocess.run(["ninja", "-C", build, "-t", "compdb"], capture_output=True, text=True, check=True).stdout)
    for entry in db:
        if entry["file"].endswith("forever/src/TbScripts.cpp"):
            args = shlex.split(entry["command"])
            out = []
            skip = 0
            for i, a in enumerate(args):
                if skip:
                    skip -= 1
                    continue
                if a in ("-o", "-MF", "-MT", "-MQ", "-include-pch"):
                    skip = 1
                    continue
                if a in ("-c", "-MD", "-MMD") or a.endswith("TbScripts.cpp") or a.endswith("ccache"):
                    continue
                if a == "-Xclang" and i + 1 < len(args) and args[i + 1] in ("-include-pch", "-include"):
                    skip = 3  # -Xclang -include-pch -Xclang <file>
                    continue
                if a == "-include":
                    skip = 1
                    continue
                out.append(a)
            return out, entry["directory"]
    sys.exit("TbScripts.cpp is not in the compile database: configure the core with -DBUILD_TORTOISEBOTS=1 first")


def sources(only):
    # FOREVER_CENSUS_LIST: a file (path relative to the repository) with one source path per line; only those are
    # checked. Lines starting with # and empty lines are ignored. Used to measure a slice of the tree.
    listed = os.environ.get("FOREVER_CENSUS_LIST", "")
    if listed:
        wanted = []
        for line in open(os.path.join(ROOT, listed)):
            line = line.strip()
            if line and not line.startswith("#"):
                wanted.append(os.path.normpath(line))
        missing = [w for w in wanted if not os.path.exists(os.path.join(ROOT, w))]
        if missing:
            sys.exit("census list names files that do not exist: " + ", ".join(missing[:10]))
        return sorted(w for w in wanted if w.endswith(".cpp") and (not only or only in w))
    found = []
    for d, _, names in os.walk(ROOT):
        rel = os.path.relpath(d, ROOT)
        if rel.startswith(SKIP_DIRS):
            continue
        for n in names:
            if n.endswith(".cpp"):
                p = os.path.normpath(os.path.join(rel, n))
                if not only or only in p:
                    found.append(p)
    return sorted(found)


ERROR = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: (?P<sev>fatal error|error): (?P<msg>.*)$")
QUOTED = re.compile(r"'([^']*)'")


def common_flags(stubs):
    flags = ["-w", "-DMANGOSBOT_ZERO=1", "-DCMANGOS=1",
             "-I" + os.path.join(ROOT, "forever", "compat"), "-I" + os.path.join(ROOT, "forever", "src", "compat")]
    if stubs:
        flags += ["-idirafter", stubs]
    for d in OLD_INCLUDE_DIRS:
        flags += ["-idirafter", os.path.join(ROOT, d)]
    return flags


PREINCLUDE = os.path.join(ROOT, "forever", "src", "compat", "botpch_forever.h")


def build_pch(command, cwd, stubs, out):
    """Compile the pre-include header once. Every file then loads it instead of parsing the core's headers again.
    Returns the .pch path, or "" when it could not be built (the census then includes the header as text)."""
    pch = os.path.join(out, "botpch_forever.pch")
    if os.path.exists(pch):
        os.remove(pch)
    run = subprocess.run(command + common_flags(stubs) + ["-x", "c++-header", PREINCLUDE, "-o", pch],
                         cwd=cwd, capture_output=True, text=True)
    if run.returncode != 0 or not os.path.exists(pch):
        sys.stderr.write("pre-include header did not precompile, falling back to a text include:\n" + run.stderr[-600:] + "\n")
        return ""
    return pch


def check(args):
    command, cwd, src, stubs, pch = args
    # The Turtle build puts ai/botpch.h in front of every file; here it is the ForeverCore counterpart, precompiled.
    extra = ["-fsyntax-only", "-ferror-limit=0"] + common_flags(stubs)
    extra += ["-include-pch", pch] if pch else ["-include", PREINCLUDE]
    run = subprocess.run(command + extra + [os.path.join(ROOT, src)], cwd=cwd, capture_output=True, text=True)
    errors = []
    for line in run.stderr.splitlines():
        m = ERROR.match(line)
        if m:
            where = os.path.relpath(m.group("file"), ROOT) if m.group("file").startswith(ROOT) else m.group("file")
            errors.append((m.group("sev"), m.group("msg"), where + ":" + m.group("line")))
    return src, run.returncode, errors


def main():
    build, out = sys.argv[1], sys.argv[2]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    only = sys.argv[4] if len(sys.argv) > 4 else ""
    os.makedirs(out, exist_ok=True)
    command, cwd = base_command(build)
    # A missing header stops a file at once. To see what is behind it, the census gives every header that has no
    # ForeverCore counterpart an empty stand-in (in <out>/stubs, never in the repository) and tries again.
    stubs = os.path.join(out, "stubs")
    os.makedirs(stubs, exist_ok=True)
    todo = sources(only)
    per_file, results, fatal = {}, {}, collections.Counter()
    for _ in range(6):
        if not todo:
            break
        again = []
        # A new stand-in header changes what the pre-include header sees, so it is precompiled again each round.
        pch = "" if os.environ.get("FOREVER_CENSUS_NO_PCH") else build_pch(command, cwd, stubs, out)
        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            for src, rc, errors in pool.map(check, [(command, cwd, s, stubs, pch) for s in todo]):
                results[src] = (rc, errors)
                new_stub = False
                for sev, msg, _ in errors:
                    m = re.match(r"'([^']+)' file not found", msg)
                    if sev == "fatal error" and m:
                        fatal[m.group(1)] += 1
                        path = os.path.join(stubs, m.group(1))
                        if not os.path.exists(path):
                            os.makedirs(os.path.dirname(path), exist_ok=True)
                            open(path, "w").write("// census stand-in for a header ForeverCore does not have\n")
                        new_stub = True
                if new_stub:
                    again.append(src)
        todo = again
    # A mistake in a header is reported once for every file that includes it. `seen` keeps each place once, so the
    # totals are places in the source, and where.tsv shows which files (mostly headers) hold them.
    kinds, names, where_count, seen = collections.Counter(), collections.Counter(), collections.Counter(), set()
    for src, (rc, errors) in results.items():
        per_file[src] = (rc, len(errors))
        for sev, msg, where in errors:
            if (where, msg) in seen:
                continue
            seen.add((where, msg))
            where_count[where.rsplit(":", 1)[0]] += 1
            kinds[QUOTED.sub("'_'", msg)] += 1
            for name in QUOTED.findall(msg)[:1]:
                names[name] += 1
    clean = sorted(s for s, (rc, n) in per_file.items() if rc == 0)
    with open(os.path.join(out, "files.tsv"), "w") as f:
        for s, (rc, n) in sorted(per_file.items(), key=lambda kv: (kv[1][1], kv[0])):
            f.write(f"{n}\t{rc}\t{s}\n")
    for name, counter in (("kinds.tsv", kinds), ("names.tsv", names), ("fatal.tsv", fatal), ("where.tsv", where_count)):
        with open(os.path.join(out, name), "w") as f:
            for key, n in counter.most_common():
                f.write(f"{n}\t{key}\n")
    total = len(seen)
    summary = (f"files {len(per_file)}  compile clean {len(clean)}  with errors {len(per_file) - len(clean)}  "
               f"error places {total} (in {len(where_count)} files)  includes of a header ForeverCore does not have {sum(fatal.values())}\n")
    open(os.path.join(out, "summary.txt"), "w").write(summary)
    open(os.path.join(out, "clean.txt"), "w").write("\n".join(clean) + "\n")
    print(summary, end="")


if __name__ == "__main__":
    main()
