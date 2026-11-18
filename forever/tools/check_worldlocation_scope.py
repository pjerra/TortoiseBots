#!/usr/bin/env python3
"""Check that the WorldLocation macro (forever/src/compat/TbCompat_Position.h) never touches a core header.

    python3 forever/tools/check_worldlocation_scope.py <core build dir> [jobs]      (FOREVER_CENSUS_LIST as for the census)

The shim ends by `#define WorldLocation TbLegacyWorldLocation`, so the token means the legacy struct in every legacy source
file. That is right for the bot's code and wrong for a CORE header that is read after the define: botpch_forever.h includes
every core header the bot uses BEFORE the shim, but a legacy file could include another one. This preprocesses each source with
the census flags (the pre-include header as text, no precompiled header) and reports every file outside the repository whose
text, after preprocessing, holds the token TbLegacyWorldLocation. No output line = clean. Run it where the census runs.
"""
import concurrent.futures, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import port_census as pc

TOKEN = "TbLegacyWorldLocation"


def check(args):
    command, cwd, src, stubs = args
    extra = ["-E"] + pc.common_flags(stubs) + ["-include", pc.PREINCLUDE]
    run = subprocess.run(command + extra + [os.path.join(pc.ROOT, src)], cwd=cwd, capture_output=True, text=True)
    current, bad = "", set()
    for line in run.stdout.splitlines():
        m = re.match(r'^# \d+ "([^"]+)"', line)
        if m:
            current = m.group(1)
            continue
        if TOKEN in line and current and not current.startswith(pc.ROOT) and "/forever/" not in current:
            bad.add(current)
    return src, run.returncode, sorted(bad)


def main():
    build = sys.argv[1]
    jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    command, cwd = pc.base_command(build)
    # The census keeps stand-ins for headers ForeverCore lacks in its own output folder; recreate the ones it would need.
    stubs = os.path.join("/tmp", "wl-scope-stubs")
    os.makedirs(stubs, exist_ok=True)
    sources = pc.sources("")
    bad_total, failed = {}, []
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for src, rc, bad in pool.map(check, [(command, cwd, s, stubs) for s in sources]):
            if rc != 0:
                failed.append(src)
            for header in bad:
                bad_total.setdefault(header, []).append(src)
    for header, users in sorted(bad_total.items()):
        print(f"CORE HEADER UNDER THE MACRO: {header}  (first seen from {users[0]}, {len(users)} files)")
    for src in failed:
        print(f"could not be preprocessed (a header the census would stand in for): {src}")
    print(f"checked {len(sources)} files, {len(failed)} could not be preprocessed, {len(bad_total)} core headers under the macro")


if __name__ == "__main__":
    main()
