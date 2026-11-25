#!/usr/bin/env python3
"""Headless raid-groups check: a raid of host bots forms through `.tb party` and takes the raid layout (no game client).

    python3 forever/tools/raid_groups_check.py --container <world container> LEADER MEMBER... [--commander NAME] [--offline MEMBER] [--keep]

Needs a world server built with TORTOISEBOTS_SLICE1, TortoiseBots.Raid.Enable = 1, and 5 to 39 logged-in host bots
besides LEADER that are in no group; at least one of them (or LEADER) has the tank role. The console is reached the way
the Waterys Island test server's world.sh does it: a line written to /run/fc/world.in inside the container, replies read
back from the container log by the `#token` every `.tb` reply carries.

Steps and what must hold:
  1. `.tb party LEADER MEMBER...`  every member joined, the group is a raid (`raid=yes`)
  2. --commander NAME: `.tb master party LEADER NAME` (a bot cannot command itself: NAME is a character outside the raid, a
     GM's own one in the game); the bots get their engines, so tank healers can take their focus
  3. `.tb layout LEADER`           every member placed (`members=` = all, `misplaced=0`), exactly one main tank; with
                                   --commander every tank healer has `focus=yes`
  4. `.tb party LEADER`            the roster's subgroups are the layout's, no subgroup holds more than five, the main
                                   tank carries the `maintank` mark and each off-tank the `assistant` mark
  5. --offline MEMBER: `.tb logout MEMBER`, then `.tb layout LEADER` again and the roster: no subgroup holds more than five
     (review #187 item 1). A host bot leaves its raid when it leaves the world, so only a REAL player's character stays as an
     offline member: with a host bot the check reports that it left instead of judging its seat; afterwards MEMBER logs in again
  6. unless --keep: `.tb ungroup` for every member (the leader's group dissolves with them)
Exit 0 on PASS, 1 on FAIL, 2 on a usage or console error. Prints every reply line it judged.
A dev-box tool (docker exec, a container path): not part of tools/verify_all.sh.
"""
import argparse
import re
import subprocess
import sys
import time
from collections import Counter


class Console:
    def __init__(self, container):
        self.container = container
        self.token = int(time.time()) % 100000

    def send(self, line):
        subprocess.run(["timeout", "5", "docker", "exec", "-i", self.container, "sh", "-c", "cat > /run/fc/world.in"],
                       input=(line + "\n").encode(), check=True)

    def tb(self, line, timeout=20.0):
        self.token += 1
        tok = f"#rg{self.token}"
        since = "%.3f" % (time.time() - 1)
        self.send(f"tb {line} {tok}")
        t0 = time.time()
        while True:
            time.sleep(0.5)
            out = subprocess.run(["docker", "logs", "--since", since, self.container], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, stdin=subprocess.DEVNULL).stdout
            lines = [re.sub(r"^(?:\x07?TC>\s*)+", "", l).strip() for l in out.splitlines()]
            lines = [l[: -len(tok)].rstrip() for l in lines if l.endswith(tok)]
            if any(re.match(r"^(\w+-end |error )", l) for l in lines) or time.time() - t0 > timeout:
                return lines


def kv(line):
    return dict(re.findall(r"(\w+)=(\S*)", line))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--container", required=True)
    ap.add_argument("--keep", action="store_true", help="leave the raid formed")
    ap.add_argument("--commander", metavar="NAME", help="run `.tb master party LEADER NAME` before the layout and expect focus=yes")
    ap.add_argument("--offline", metavar="MEMBER", help="log this member out and check the layout's five-per-subgroup cap again")
    ap.add_argument("names", nargs="+")
    a = ap.parse_args()
    if not 6 <= len(a.names) <= 40:
        print("give the leader and 5 to 39 members (a raid has 6 to 40)")
        return 2
    leader, members = a.names[0], a.names[1:]
    c = Console(a.container)
    fails = []

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            fails.append(what)

    formed = c.tb(f"party {leader} " + " ".join(members))
    for l in formed:
        print("  " + l)
    end = next((kv(l) for l in formed if l.startswith("party-end ")), {})
    check(end.get("joined") == str(len(members)), f"party: {end.get('joined')} of {len(members)} joined")
    check(end.get("raid") == "yes", "party: the group is a raid")

    if a.offline and a.offline not in members:
        print("--offline names a member of the list")
        return 2
    if a.commander:
        for l in c.tb(f"master party {leader} {a.commander}"):
            print("  " + l)
        time.sleep(3)   # the engines are built on the next bot ticks

    laid = c.tb(f"layout {leader}")
    for l in laid:
        print("  " + l)
    rows = {kv(l)["member"]: kv(l) for l in laid if l.startswith("layout member=")}
    lend = next((kv(l) for l in laid if l.startswith("layout-end ")), {})
    check(lend.get("members") == str(len(a.names)), f"layout: {lend.get('members')} of {len(a.names)} members placed")
    check(lend.get("misplaced") == "0", f"layout: misplaced={lend.get('misplaced')}")
    mains = [n for n, r in rows.items() if r.get("tank") == "main"]
    check(len(mains) == 1, f"layout: one main tank ({','.join(mains) or 'none'})")
    healers = [n for n, r in rows.items() if r.get("heals") not in (None, "", "raid")]
    if a.commander:
        check(all(rows[n].get("focus") == "yes" for n in healers), f"layout: {len(healers)} tank healer(s) took their focus")

    roster = c.tb(f"party {leader}")
    for l in roster:
        print("  " + l)
    seen = {kv(l)["member"]: kv(l) for l in roster if l.startswith("party member=")}
    check(set(seen) >= set(rows), "roster: every layout member is in the raid")
    check(all(seen.get(n, {}).get("sub") == r.get("sub") for n, r in rows.items()), "roster: subgroups are the layout's")
    per_sub = Counter(r.get("sub") for r in seen.values())
    check(all(n <= 5 for n in per_sub.values()), "roster: no subgroup holds more than five (" +
          ", ".join(f"{s}:{n}" for s, n in sorted(per_sub.items())) + ")")
    check(all(seen.get(n, {}).get("mark") == "maintank" for n in mains), "roster: the main tank carries the maintank mark")
    offs = [n for n, r in rows.items() if r.get("tank") == "off"]
    check(all(seen.get(n, {}).get("mark") == "assistant" for n in offs), f"roster: {len(offs)} off-tank(s) carry the assistant mark")

    def roster_rows():
        return {kv(l)["member"]: kv(l) for l in c.tb(f"party {leader}") if l.startswith("party member=")}

    if a.offline:
        seat = seen.get(a.offline, {}).get("sub")
        c.send(f"tb logout {a.offline}")
        for _ in range(30):
            time.sleep(1)
            now = roster_rows()
            if now.get(a.offline, {}).get("online") == "no":
                break
        stayed = now.get(a.offline, {}).get("online") == "no"
        print(f"note offline: {a.offline} " + ("is an offline member of the raid" if stayed else "left the raid when it left the world (a host bot does)"))
        again = c.tb(f"layout {leader}")
        for l in again:
            print("  " + l)
        now = roster_rows()
        for n, r in sorted(now.items()):
            print(f"  roster {n} sub={r.get('sub')} online={r.get('online')}")
        per = Counter(r.get("sub") for r in now.values())
        check(all(n <= 5 for n in per.values()), "offline: no subgroup holds more than five (" +
              ", ".join(f"{s}:{n}" for s, n in sorted(per.items())) + ")")
        if stayed:
            check(now.get(a.offline, {}).get("sub") == seat, f"offline: {a.offline} kept its seat (sub={seat})")
        c.send(f"tb login {a.offline}")
        for _ in range(40):
            time.sleep(1)
            if roster_rows().get(a.offline, {}).get("online") == "yes":
                break

    if not a.keep:
        for m in members:
            c.send(f"tb ungroup {m}")
            time.sleep(0.2)
    print("PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as e:
        print(f"console not reachable: {e}")
        sys.exit(2)
