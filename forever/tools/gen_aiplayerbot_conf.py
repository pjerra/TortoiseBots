#!/usr/bin/env python3
"""Generate forever/conf/aiplayerbot.conf.dist and docs/server/config-keys.md.

    python3 forever/tools/gen_aiplayerbot_conf.py            # write both files
    python3 forever/tools/gen_aiplayerbot_conf.py --check    # exit 1 when either file is stale or lists an unread key

Every `config.Get{Bool,Int,Float,String}Default("Key", default)` call in ai/playerbot/PlayerbotAIConfig.cpp is one
advertised key: the key, the default the code has and its type. A key is shipped only when the Forever build consumes
it: its exact name is read by a translation unit the build compiles (or a header one of those units includes) and the
PlayerbotAIConfig member that receives the value is named there too, or the value is used at the read itself (a cast, a
clamp, a setter); the same rule and
attribution code as tools/verify_config_usage.py decides that, not a second scan. A key the loader reads but no compiled
file consumes would be a knob that does nothing, so it is left out of the config file and named, with the reason, in
docs/server/config-keys.md.

The build definition, read on every run (CMakeLists.txt):
- :31-33 globs forever/src (GLOB_RECURSE);
- :39 drops the slice and TbHost files from that glob, :55 (slice ON) drops the option-OFF stub TbH3Off.cpp, and that
  same `else` branch compiles forever/src/slice/*.cpp and forever/src/host/TbHost*.cpp (:113-114);
- :85-111 appends every path of forever/SLICE-W4.txt except TB_SLICE_REPLACED (:93-95);
- the slice block adds runtime/CombatMonitorService.cpp and the run-trace/why-trace runtime sources
  (runtime/RunTrace.cpp, runtime/RunTraceWriter.cpp, runtime/WhyTrace.cpp) and the decision capture
  (runtime/DecisionCapture.cpp, runtime/DecisionCaptureFiles.cpp);
- :179 ships this config when TORTOISEBOTS_SLICE1 is ON, the build analysed here (the option's default is OFF at :43).
Headers are compiled through the units that include them, so the file set is closed over local `#include "..."` using
the include directories at :60-63, :139-149 and the preinclude at :86.

A call whose default is not a literal (an expression, a variable read earlier) is written commented out with the
expression: the code's own default applies when the key is absent, so such a key is left for the owner to set. A key read
more than once is written once (the first call); a later call with a different default is noted in a comment. Calls whose
key is not a string literal are counted in the header, not listed. The file is read by TbCompat_Config (ACE ini rules:
one section, `#` comments on their own line, `key = value`), see forever/docs/SLICE1-R4C-NOTES.md.
"""
import glob
import importlib.util
import os
import pathlib
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SOURCE = os.path.join(ROOT, "ai", "playerbot", "PlayerbotAIConfig.cpp")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")
OUT = os.path.join(ROOT, "forever", "conf", "aiplayerbot.conf.dist")
DOCS_OUT = os.path.join(ROOT, "docs", "server", "config-keys.md")
KNOBS = os.path.join(ROOT, "forever", "data", "run-knobs-139.tsv")

CALL = re.compile(r"\bconfig\.Get(Bool|Int|Float|String)Default\s*\(")

# Keys whose Forever default is a core-guarded constant in PlayerbotAIConfig.cpp, so the census cannot read the value
# from the call. The value here is what the Forever build's own default is; the shared template keeps the Turtle value.
FOREVER_DEFAULTS = {
    # Forever's pool may start a fresh bot at level 1; the Turtle build keeps TB's 5.
    "AiPlayerbot.RandomBotStartLevelMin": "1",
}


def split_args(text, start):
    """Arguments of the call whose '(' is just before `start`: (list of argument texts, index after the ')')."""
    depth, i, args, cur = 1, start, [], []
    in_str = None
    while i < len(text):
        c = text[i]
        if in_str:
            cur.append(c)
            if c == "\\":
                i += 1
                cur.append(text[i])
            elif c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i + 1
            cur.append(c)
        elif c == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    raise ValueError("unbalanced call")


def literal(kind, expr):
    """The conf text of a literal default, or None when the default is an expression."""
    e = expr.strip()
    if kind == "Bool":
        return {"true": "1", "false": "0"}.get(e)
    if kind == "Int":
        m = re.fullmatch(r"\(?\s*(?:u?int(?:8|16|32|64)(?:_t)?|int|unsigned|long)?\s*\)?\s*(-?\d+)[uUlL]*", e)
        if m:
            return m.group(1)
        # integer arithmetic of plain numbers (6 * 3600)
        if re.fullmatch(r"[\d\s\*\+\-\(\)]+", e) and re.search(r"\d", e):
            try:
                return str(int(eval(e, {"__builtins__": {}}, {})))
            except Exception:
                return None
        return None
    if kind == "Float":
        m = re.fullmatch(r"(-?\d+(?:\.\d*)?|-?\.\d+)[fF]?", e)
        if not m:
            return None
        v = m.group(1)
        return v if "." in v else v + ".0"
    if kind == "String":
        if e in ('""', "std::string()", "std::string(\"\")"):
            return ""
        # one string literal or several side by side ("a" "b")
        if re.fullmatch(r'(?:"(?:[^"\\]|\\.)*"\s*)+', e):
            text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', e))
            if "\\" not in text:
                return text
    return None


def collect(text):
    entries, order, nonliteral_keys = {}, [], 0
    for m in CALL.finditer(text):
        kind = m.group(1)
        args, _ = split_args(text, m.end())
        line = text.count("\n", 0, m.start()) + 1
        if not args or not re.fullmatch(r'"[^"\\]+"', args[0]):
            nonliteral_keys += 1
            continue
        key = args[0][1:-1]
        default = args[-1] if len(args) >= 2 else ""
        lit = literal(kind, default)
        entry = (kind, key, lit, default, line)
        if key in entries:
            first = entries[key]
            if (first["lit"], first["kind"]) != (lit, kind):
                first["notes"].append("line %d reads it as %s with default %s" % (line, kind, default))
            continue
        entries[key] = {"kind": kind, "lit": lit, "expr": default, "line": line, "notes": []}
        order.append(key)
    for key, value in FOREVER_DEFAULTS.items():
        if key not in entries:
            sys.exit("FOREVER_DEFAULTS names %s, which PlayerbotAIConfig.cpp no longer reads" % key)
        entries[key]["lit"] = value
    return entries, order, nonliteral_keys


def _read(path):
    return pathlib.Path(path).read_text(encoding="utf-8", errors="replace")


def _relative(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def _cmake(pattern, text, what):
    m = re.search(pattern, text, re.S)
    if not m:
        sys.exit("CMakeLists.txt no longer has %s; update forever/tools/gen_aiplayerbot_conf.py" % what)
    return m


def _glob_recurse(pattern):
    """CMake GLOB_RECURSE: `dir/*.ext` matches the extension at any depth."""
    directory, _, leaf = pattern.rpartition("/")
    suffix = leaf.lstrip("*")
    return [p for p in pathlib.Path(ROOT, directory).rglob("*" + suffix) if p.is_file()]


def compiled_sources():
    """(translation units, header include directories) of the Forever build with the slice ON.

    Every list comes from CMakeLists.txt so a build-definition change cannot leave this census stale silently; the
    citation for each rule is in the module docstring.
    """
    text = _read(CMAKE)
    globs = _cmake(r"file\(GLOB_RECURSE\s+PRIVATE_SOURCES\s+CONFIGURE_DEPENDS(.*?)\)", text,
                   "the forever/src glob").group(1)
    patterns = re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\"\s]+)", globs)
    sources = set()
    for pattern in patterns:
        if pattern.endswith(".cpp"):
            sources.update(_glob_recurse(pattern))

    exclusions = [
        # :39, unconditional: the slice and TbHost files are added back by the slice block below.
        (r"/forever/src/(slice/[^/]*|host/TbHost[^/]*)\\.cpp$", "the slice/TbHost glob exclusion"),
        # :55, the `else` of `if(NOT TORTOISEBOTS_SLICE1)`: the option-OFF stub is not compiled with the slice.
        (r"/forever/src/host/TbH3Off\\.cpp$", "the TbH3Off exclusion"),
    ]
    for cmake_pattern, what in exclusions:
        _cmake(re.escape(cmake_pattern), text, what)
        regex = re.compile(cmake_pattern.replace("\\\\", "\\"))
        sources = {p for p in sources if not regex.search("/" + _relative(p))}

    slice_list = _cmake(r"set\(TB_SLICE_LIST\s+\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^)]+)\)", text,
                        "TB_SLICE_LIST").group(1).strip()
    replaced = set(_cmake(r"set\(TB_SLICE_REPLACED(.*?)\)", text, "TB_SLICE_REPLACED").group(1).split())
    for line in _read(os.path.join(ROOT, slice_list)).splitlines():
        rel = line.strip()
        if not rel or rel.startswith("#") or rel in replaced:
            continue
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            sys.exit("%s names %s, which does not exist" % (slice_list, rel))
        sources.add(path)

    new_globs = _cmake(r"file\(GLOB\s+TB_SLICE_NEW\s+CONFIGURE_DEPENDS(.*?)\)", text,
                       "the TB_SLICE_NEW glob").group(1)
    for pattern in re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\"\s]+)", new_globs):
        sources.update(os.path.join(ROOT, p) for p in glob.glob(os.path.join(ROOT, pattern)))
    sources.add(os.path.join(ROOT, "runtime", "CombatMonitorService.cpp"))
    for trace_source in ("RunTrace.cpp", "RunTraceWriter.cpp", "WhyTrace.cpp", "DecisionCapture.cpp", "DecisionCaptureFiles.cpp"):
        sources.add(os.path.join(ROOT, "runtime", trace_source))

    include_dirs = [""]  # the repository root is a PUBLIC include directory (CMakeLists.txt:60-61)
    for block in re.findall(r"target_include_directories\(tortoisebots(?:\s+PUBLIC|\s+PRIVATE)?(.*?)\)",
                            text, re.S):
        for inc in re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\"\s)]*)", block):
            if inc:
                include_dirs.append(inc)
    old_dirs = _cmake(r"set\(TB_SLICE_OLD_DIRS(.*?)\)", text, "TB_SLICE_OLD_DIRS").group(1)
    include_dirs.extend(old_dirs.split())
    return sorted(str(p) for p in sources), include_dirs


def include_closure(sources, include_dirs):
    """The sources plus every local header they include, relative to the repository root."""
    preinclude = os.path.join(ROOT, "forever", "src", "compat", "botpch_forever.h")
    stack = list(sources) + [preinclude]
    seen = set()
    while stack:
        path = stack.pop()
        if path in seen or not os.path.isfile(path):
            continue
        seen.add(path)
        text = _read(path)
        for m in re.finditer(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
            target = m.group(1)
            candidates = [os.path.normpath(os.path.join(os.path.dirname(path), target))]
            candidates += [os.path.normpath(os.path.join(ROOT, d, target)) for d in include_dirs]
            for candidate in candidates:
                if os.path.isfile(candidate):
                    if candidate.endswith((".h", ".hpp")):
                        stack.append(candidate)
                    break
    return sorted(_relative(p) for p in seen if p.endswith((".cpp", ".h", ".hpp")))


def load_usage_checker():
    """tools/verify_config_usage.py: the one read-attribution and consumption rule this repo has."""
    path = os.path.join(ROOT, "tools", "verify_config_usage.py")
    spec = importlib.util.spec_from_file_location("verify_config_usage", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def classify(entries, read_keys, keys_by_member, findings, engine_index, kept):
    """({key: bool wired}, {key: [reader files]}) from the shared read/consumption results.

    The key's exact name must be read by a compiled file. A key with no attributed member is then
    consumed at the read (a cast, a clamp, a setter); a key with members is wired when at least one
    member is not among the findings' dead members, or is allowlisted on purpose.
    """
    dead = {f.split(": ", 1)[0] for f in findings}
    members_of = {}
    for member, keys in keys_by_member.items():
        for key in keys:
            members_of.setdefault(key, set()).add(member)

    wired, readers = {}, {}
    for key in entries:
        members = members_of.get(key)
        wired[key] = key in read_keys and (
            not members or any(m not in dead or m in kept for m in members))
        hits = []
        for rel, tokens in engine_index.items():
            if "#" in rel or rel == "ai/playerbot/PlayerbotAIConfig.h":
                continue  # a synthetic accessor or the declaration header, not a consumer file
            if members and any(m in tokens for m in members):
                hits.append(rel)
        readers[key] = sorted(hits)
    return wired, readers


def census(entries):
    """({key: bool wired}, {key: [reader files]}) for every advertised key."""
    sources, include_dirs = compiled_sources()
    files = include_closure(sources, include_dirs)
    usage = load_usage_checker()
    keys_by_member, _ = usage.scan_reads(ROOT, files)
    findings = usage.check(ROOT, files)
    engine_index, _ = usage.consumption_index(ROOT, files)
    kept = set(usage.ALLOWLIST) | set(usage.DISPLAY_ONLY_ALLOWLIST)
    read_keys = set()
    for rel in files:
        clean = usage.strip_comments(_read(os.path.join(ROOT, rel)))
        read_keys.update(m.group(1) for m in usage.GET.finditer(clean))
    return classify(entries, read_keys, keys_by_member, findings, engine_index, kept)


def advertised_unread(conf_text, wired):
    """Keys listed in a config file that the compiled build does not consume."""
    listed = re.findall(r"^(?:#\s*)?(AiPlayerbot\.[A-Za-z0-9_.]+)\s*=", conf_text, re.M)
    return [key for key in dict.fromkeys(listed) if not wired.get(key, False)]


def load_knobs():
    """The dungeon-run knob table (forever/data/run-knobs-139.tsv): key -> (status, where). Missing file: no notes."""
    knobs = {}
    if not os.path.exists(KNOBS):
        return knobs
    for raw in open(KNOBS, encoding="utf-8"):
        if raw.startswith("#") or not raw.strip():
            continue
        cols = raw.rstrip("\n").split("\t")
        knobs["AiPlayerbot." + cols[0]] = (cols[2], cols[3])
    return knobs


def knob_note(status, where):
    """The one-line template note of a knob, or None when the key is read and needs none."""
    if status == "not-read":
        return "TBF: not read, this line has no effect (forever/docs/RUN-KNOBS-139-NOTES.md)"
    if status == "hard-coded":
        return "TBF: hard-coded to %s, the value below is ignored (forever/docs/RUN-KNOBS-139-NOTES.md)" % where
    if status == "ported-other":
        return "TBF: not read, the behaviour is covered another way (forever/docs/RUN-KNOBS-139-NOTES.md)"
    return None


def render(entries, order, nonliteral_keys, wired):
    shipped = [k for k in order if wired[k]]
    omitted = len(order) - len(shipped)
    knobs = load_knobs()
    out = []
    out.append("# TortoiseBots bot engine configuration (aiplayerbot.conf).")
    out.append("# GENERATED by forever/tools/gen_aiplayerbot_conf.py from the Get*Default calls of ai/playerbot/PlayerbotAIConfig.cpp:")
    out.append("# do not edit by hand, run the tool. Every uncommented line is a key with the default the code has; a commented line is a")
    out.append("# key whose default the code computes, so it is left to the code until it is set here.")
    out.append("# Install as aiplayerbot.conf in the directory that holds worldserver.conf (see forever/docs/SLICE1-R4C-NOTES.md).")
    out.append("# %d keys, %d with a literal default, %d computed; %d reads with a non-literal key are not listed."
               % (len(shipped), sum(1 for k in shipped if entries[k]["lit"] is not None),
                  sum(1 for k in shipped if entries[k]["lit"] is None), nonliteral_keys))
    out.append("# %d key(s) the loader reads are consumed by no compiled file and are not listed here; docs/server/config-keys.md names them."
               % omitted)
    out.append("")
    out.append("[AiPlayerbotConf]")
    out.append("")
    for key in shipped:
        e = entries[key]
        out.append("# %s  (%s, PlayerbotAIConfig.cpp:%d)" % (key, e["kind"].lower(), e["line"]))
        for note in e["notes"]:
            out.append("#   note: " + note)
        if key in knobs:
            note = knob_note(*knobs[key])
            if note:
                out.append("#   " + note)
        if e["lit"] is None:
            out.append("#   default is computed in the code: %s" % " ".join(e["expr"].split()))
            out.append("# %s =" % key)
        else:
            out.append("%s = %s" % (key, e["lit"]))
        out.append("")
    return "\n".join(out)


def _default_text(entry):
    text = entry["lit"] if entry["lit"] is not None else entry["expr"]
    text = " ".join(text.split()).replace("|", "\\|")
    return "`%s`" % text if text else "*(empty)*"


def render_docs(entries, order, wired, readers):
    shipped = [k for k in order if wired[k]]
    unread = [k for k in order if not wired[k]]
    out = []
    out.append("---")
    out.append("id: server-config-keys")
    out.append("title: Every Configuration Key and Whether the Forever Build Reads It")
    out.append("category: server")
    out.append('summary: "Generated per-key census of aiplayerbot.conf: the keys the Forever build consumes and the ones that would do nothing."')
    out.append("tags: [config, server, reference, generated]")
    out.append("relates_to:")
    out.append("  - server-configuration")
    out.append("---")
    out.append("")
    out.append("# Every Configuration Key and Whether the Forever Build Reads It")
    out.append("")
    out.append("<!-- GENERATED by forever/tools/gen_aiplayerbot_conf.py; do not edit by hand; run the tool. -->")
    out.append("")
    out.append("`ai/playerbot/PlayerbotAIConfig.cpp` advertises %d keys. The Forever build consumes %d of them and ships those "
               "in `forever/conf/aiplayerbot.conf.dist`; %d are read into the config object but no compiled file uses the "
               "value, so they are not shipped and setting one changes nothing." % (len(order), len(shipped), len(unread)))
    out.append("")
    out.append("**How this is measured.** The compiled file list comes from `CMakeLists.txt` with "
               "`TORTOISEBOTS_SLICE1=ON` (the build that ships this config): the `forever/src` glob, the "
               "`forever/SLICE-W4.txt` legacy files, the slice and host additions, and the headers those units include. "
               "A key is *wired* when its exact name is read by one of those files and the `PlayerbotAIConfig` member "
               "it fills is named there too, or when the loader consumes the value at the read itself (a cast, a "
               "clamp, a setter). The attribution and consumption rules are `tools/verify_config_usage.py`.")
    out.append("")
    out.append("## Every advertised key")
    out.append("")
    out.append("| Key | Default | Type | Wired | Consumed by |")
    out.append("| :--- | :--- | :--- | :--- | :--- |")
    for key in order:
        entry = entries[key]
        if wired[key]:
            where = readers.get(key) or []
            shown = ", ".join("`%s`" % path for path in where[:3])
            if len(where) > 3:
                shown += " +%d more" % (len(where) - 3)
            if not shown:
                shown = "the config accessor (`PlayerbotAIConfig.cpp:%d`)" % entry["line"]
            status = "yes"
        else:
            shown = "**not wired** — nothing in the Forever build reads it"
            status = "no"
        out.append("| `%s` | %s | %s | %s | %s |"
                   % (key, _default_text(entry), entry["kind"].lower(), status, shown))
    out.append("")
    return "\n".join(out)


def main():
    text = _read(SOURCE)
    entries, order, nonliteral_keys = collect(text)
    wired, readers = census(entries)
    rendered = render(entries, order, nonliteral_keys, wired)
    docs = render_docs(entries, order, wired, readers)
    shipped = sum(1 for key in order if wired[key])
    summary = ("%d keys, %d wired, %d not wired" % (len(order), shipped, len(order) - shipped))

    if "--check" in sys.argv:
        ok = True
        current = _read(OUT) if os.path.exists(OUT) else ""
        if current != rendered:
            print("aiplayerbot.conf.dist is out of step with PlayerbotAIConfig.cpp or its readers: run forever/tools/gen_aiplayerbot_conf.py")
            ok = False
        for key in advertised_unread(current, wired):
            print("UNREAD %s: listed in aiplayerbot.conf.dist but no compiled file consumes it" % key)
            ok = False
        current_docs = _read(DOCS_OUT) if os.path.exists(DOCS_OUT) else ""
        if current_docs != docs:
            print("docs/server/config-keys.md is out of step with the keys and their readers: run forever/tools/gen_aiplayerbot_conf.py")
            ok = False
        if ok:
            print("aiplayerbot.conf.dist and config-keys.md match (%s)" % summary)
            return 0
        return 1

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(rendered)
    with open(DOCS_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(docs)
    lit = sum(1 for key in order if wired[key] and entries[key]["lit"] is not None)
    print("wrote %s and %s: %s, %d literal, %d non-literal-key reads skipped"
          % (_relative(OUT), _relative(DOCS_OUT), summary, lit, nonliteral_keys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
