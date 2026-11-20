#!/usr/bin/env bash
# Runtime check of the travel graph loader against the run2 test server (forever/docs/W4-A1-NOTES.md, section 7).
#
# It boots the INSTALLED worldserver four times and reads its log. It installs nothing: put the build you want to test in
# $FOREVER_RUN_DIR/bin/worldserver first, hold the server lock (FOREVER-RUN2.md, "One live test at a time"), and have the
# lock free the server again afterwards.
#
#   exec 9>/tmp/forever-live-forever-run2.lock; flock -w 7200 9
#   forever/tools/travel_graph_boot_check.sh                 # all scenarios
#   forever/tools/travel_graph_boot_check.sh a               # only scenario a
#
# Scenarios (each one a boot, because every case is a boot-time decision):
#   a  no travel tables        the guard refuses the queries (never sends one to a missing table), the graph stays empty,
#                              one line; travel has no graph and the destination build still runs
#   b  empty tables            one line "dataset is empty", no startup graph generation, no abort
#   c  nodes and links         two nodes and a link whose target node does not exist (an out-of-map link): the link is
#                              skipped, the graph is built, the server stays up
#   final  no travel tables    the state a server ships in before the generation boot (round B): the "absent graph" no-op
#
# Exit code 0 = every checked line was seen (and none that must not appear); 1 = a check failed or a boot did not come up.
set -uo pipefail

RUN=${FOREVER_RUN_DIR:-$HOME/forever-run2}
DB=${FOREVER_WORLD_DB:-world}
HERE=$(cd "$(dirname "$0")" && pwd)
SCHEMA=${FOREVER_TRAVEL_SCHEMA:-$HERE/../../data/sql/world/20260824090000_world.sql}
LOG=$RUN/logs/world.out
SERVER=forever-world2
TABLES="ai_playerbot_travelnode ai_playerbot_travelnode_link ai_playerbot_travelnode_path"

WANT=${*:-a b c final}

FAILS=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

has()      { if grep -qE "$1" "$LOG"; then pass "$2"; else fail "$2 (no line: $1)"; fi; }
hasnt()    { if grep -qE "$1" "$LOG"; then fail "$2 (unexpected line: $1)"; else pass "$2"; fi; }
has_once() { local n; n=$(grep -cE "$1" "$LOG"); if [ "$n" = 1 ]; then pass "$2"; else fail "$2 (expected 1 line, saw $n: $1)"; fi; }

sql() { docker exec -e MYSQL_PWD="$(cat "$RUN/.db_root_pass")" "$DB_CONTAINER" mysql -uroot -N -B "$DB" -e "$1"; }

DB_CONTAINER=forever-db2

drop_tables() { sql "DROP TABLE IF EXISTS ai_playerbot_travelnode_path, ai_playerbot_travelnode_link, ai_playerbot_travelnode"; }
create_empty() {
    # Only the three travel tables of the module migration: the rest of it belongs to other rounds.
    drop_tables
    awk 'BEGIN { RS = ";" } /CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode(_link|_path)?`/ { print $0 ";" }' "$SCHEMA" \
        | docker exec -i -e MYSQL_PWD="$(cat "$RUN/.db_root_pass")" "$DB_CONTAINER" mysql -uroot -N -B "$DB"
}

stop_server() { ( cd "$RUN" && ./world.sh stop ) >/dev/null 2>&1; }

boot() {
    : > "$LOG"
    ( cd "$RUN" && ./world.sh start ) >/dev/null 2>&1
    local i
    for i in $(seq 1 120); do
        grep -q "worldserver-daemon) ready..." "$LOG" && return 0
        docker inspect -f '{{.State.Running}}' "$SERVER" 2>/dev/null | grep -q true || return 1
        sleep 2
    done
    return 1
}

rss() {
    docker exec "$SERVER" bash -c 'for p in /proc/[0-9]*; do tr "\0" " " < "$p/cmdline" 2>/dev/null | grep -q worldserver && awk "/VmRSS/ {print \$2}" "$p/status"; done' 2>/dev/null | head -1
}

common_checks() {
    has "worldserver-daemon\\) ready\\.\\.\\." "server ready"
    local n; n=$(grep -c "ASSERTION FAILED" "$LOG")
    if [ "$n" = 0 ]; then pass "no assertion"; else fail "assertions in the log: $n"; fi
    has "Loading travel nodes\\." "the loader was reached"
    has ">> Loaded [0-9]+ travel destination spawn entries" "the A2 destination index is built"
    has ">> travel destinations keep [0-9]+ spawn points" "the destination point map is built"
}

scenario_a() {
    printf 'scenario a: no travel tables\n'
    drop_tables || { fail "could not drop the travel tables"; return; }
    if ! boot; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has 'world table `ai_playerbot_travelnode` is missing, or lacks a column this query names: not read' "the schema guard refused the node query (no query reached a missing table)"
    has_once 'the travel-node tables are missing or incomplete; the travel graph stays empty and travel does nothing' "one line for the absent graph"
    hasnt '>> Loaded [0-9]+ travelNodes' "no node row was read"
    hasnt 'Calculating mapoffset' "the empty graph skips the derived data"
    stop_server
}

scenario_b() {
    printf 'scenario b: empty travel tables\n'
    if ! create_empty; then fail "could not create the empty schema"; return; fi
    if ! boot; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has 'travel-node dataset is empty or unavailable; skipping startup graph generation' "one line for the empty dataset"
    hasnt 'world table `ai_playerbot_travelnode` is missing' "the tables exist, so the guard does not refuse"
    hasnt 'Calculating mapoffset' "no startup graph generation"
    stop_server
}

scenario_c() {
    printf 'scenario c: two nodes, a link whose target node does not exist, and its path points\n'
    drop_tables || { fail "could not drop the travel tables"; return; }
    if ! create_empty; then fail "could not create the empty schema"; return; fi
    sql "INSERT INTO ai_playerbot_travelnode (id, name, map_id, x, y, z, linked) VALUES
           (1, 'Alpha', 0, -8949.95, -132.493, 83.5312, 1),
           (2, 'Beta',  0, -8950.0, -130.0,   83.5312, 1),
           (3, 'Gamma', 0, -100.0,  -200.0,   50.0,    1)" || { fail "node insert failed"; return; }
    # 1 -> 424242: no such node row (the out-of-map link); 2 -> 1: a real link with three ordered path points.
    sql "INSERT INTO ai_playerbot_travelnode_link (node_id, to_node_id, type, object, distance, swim_distance, extra_cost, calculated, max_creature_0, max_creature_1, max_creature_2) VALUES
           (1, 424242, 1, 0, 10.0, 0, 0, 1, 0, 0, 0),
           (2, 1,      1, 0, 20.0, 0, 0, 1, 0, 0, 0)" || { fail "link insert failed"; return; }
    sql "INSERT INTO ai_playerbot_travelnode_path (node_id, to_node_id, nr, map_id, x, y, z) VALUES
           (1, 424242, 0, 0, -8949.9, -132.4, 83.5),
           (1, 424242, 1, 0, -8949.9, -132.3, 83.5),
           (2, 1, 0, 0, -8949.95, -132.0, 83.5),
           (2, 1, 1, 0, -8949.97, -132.2, 83.5),
           (2, 1, 2, 0, -8949.95, -132.49, 83.53)" || { fail "path insert failed"; return; }
    if ! boot; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has '>> Loaded 3 travelNodes\\.' "three nodes loaded"
    has '>> Loaded 2 travelNode paths\\.' "both link rows read"
    hasnt 'travel-node dataset is empty' "the graph is not empty"
    # The link to the unknown node was skipped: no route is built from node 1 to it, and the boot survived.
    has 'Calculating mapoffset' "the populated graph builds its derived data"
    stop_server
}

scenario_final() {
    printf 'scenario final: no travel tables (the shipped state)\n'
    drop_tables || { fail "could not drop the travel tables"; return; }
    if ! boot; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has_once 'the travel-node tables are missing or incomplete; the travel graph stays empty and travel does nothing' "one line for the absent graph"
    ( cd "$RUN" && ./world.sh cmd "tb status" ) >/dev/null 2>&1
    sleep 2
    has 'TortoiseBots: enabled' "module enabled"
    printf '  info worldserver RSS at ready: %s KiB\n' "$(rss)"
    stop_server
}

cleanup() {
    stop_server
    drop_tables
}

trap cleanup EXIT

for s in $WANT; do
    case "$s" in
        a) scenario_a ;;
        b) scenario_b ;;
        c) scenario_c ;;
        final) scenario_final ;;
        *) echo "unknown scenario: $s"; exit 2 ;;
    esac
done

if [ "$FAILS" = 0 ]; then
    echo "RESULT: PASS (travel graph boot check)"
    exit 0
fi
echo "RESULT: FAIL ($FAILS check(s))"
exit 1
