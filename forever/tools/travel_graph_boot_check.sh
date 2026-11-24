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
# Needs a scratch database (the check drops the travel tables in it): FOREVER_WORLD_DB=world_scratch, with $FOREVER_RUN_DIR/etc/
# worldserver.conf (WorldDatabaseInfo) pointing the server at it. It refuses to run otherwise, before anything is dropped.
#
# Scenarios (each one a boot, because every case is a boot-time decision):
#   a  no travel tables        the guard refuses the queries (never sends one to a missing table), the graph stays empty,
#                              one line; travel has no graph and the destination build still runs
#   b  empty tables            one line "dataset is empty", then the first-boot generation builds and saves the graph; a
#                              second boot loads the saved rows and does not generate again
#   c  two-continent graph     five nodes on maps 0 and 1: a link to a missing node, a "walk" between the two maps (both refused
#                              at load and counted), a zeppelin (loaded, not a route); the log line of what each node reaches
#                              shows no continent reaching the other; the server stays up
#   final  no travel tables    the state a server ships in before the generation boot (round B): the "absent graph" no-op
#
# Exit code 0 = every checked line was seen (and none that must not appear); 1 = a check failed or a boot did not come up.
set -uo pipefail

RUN=${FOREVER_RUN_DIR:-$HOME/forever-run2}
# This check DROPS the three travel tables (at the start of a scenario and in the exit trap), so it runs only against a scratch
# world database: FOREVER_WORLD_DB must name one (its name contains "scratch") and the server under $RUN must actually be
# configured to use it (refuse_unless_scratch below). There is no default: the real `world` database is never touched.
DB=${FOREVER_WORLD_DB:-}
HERE=$(cd "$(dirname "$0")" && pwd)
SCHEMA=${FOREVER_TRAVEL_SCHEMA:-$HERE/../../data/sql/world/20260824090000_world.sql}
LOG=$RUN/logs/world.out
BOTLOG=$RUN/logs/bot.log
SERVER=forever-world2
TABLES="ai_playerbot_travelnode ai_playerbot_travelnode_link ai_playerbot_travelnode_path"

WANT=${*:-a b c final}

FAILS=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

# Module lines land in two files: the bot brain logs through BotLog (logs/bot.log), the forever/src compat code
# through the core logger (logs/world.out); every check reads both.
has()      { if grep -qE "$1" "$LOG" "$BOTLOG"; then pass "$2"; else fail "$2 (no line: $1)"; fi; }
hasnt()    { if grep -qE "$1" "$LOG" "$BOTLOG"; then fail "$2 (unexpected line: $1)"; else pass "$2"; fi; }
has_once() { local n; n=$(grep -hE "$1" "$LOG" "$BOTLOG" | wc -l); if [ "$n" = 1 ]; then pass "$2"; else fail "$2 (expected 1 line, saw $n: $1)"; fi; }

sql() { docker exec -e MYSQL_PWD="$(cat "$RUN/.db_root_pass")" "$DB_CONTAINER" mysql -uroot -N -B "$DB" -e "$1"; }

DB_CONTAINER=forever-db2

# The refusal that keeps the DROP TABLEs away from a real database. Two conditions, both required: the database NAME is a scratch
# one, and the server's own config names that same database (a scratch name with a server still on `world` would drop nothing
# the server uses, but the other way round - a scratch name in the shell and the server on `world` - would test the wrong thing).
refuse_unless_scratch() {
    case "$DB" in
        *scratch*) ;;
        *) echo "REFUSED: FOREVER_WORLD_DB='$DB' is not a scratch database (its name must contain 'scratch'); this check drops the travel tables." >&2; exit 2 ;;
    esac
    local conf
    for conf in "$RUN/etc/worldserver.conf" "$RUN/worldserver.conf"; do
        [ -f "$conf" ] && break
        conf=
    done
    if [ -z "$conf" ]; then
        echo "REFUSED: no worldserver.conf under $RUN to confirm the server uses '$DB'." >&2
        exit 2
    fi
    if ! grep -E '^[[:space:]]*WorldDatabaseInfo[[:space:]]*=' "$conf" | grep -qE ";${DB}\"?[[:space:]]*$"; then
        echo "REFUSED: $conf does not point WorldDatabaseInfo at '$DB'." >&2
        exit 2
    fi
}

drop_tables() { sql "DROP TABLE IF EXISTS ai_playerbot_travelnode_path, ai_playerbot_travelnode_link, ai_playerbot_travelnode"; }
create_empty() {
    # Only the three travel tables of the module migration: the rest of it belongs to other rounds.
    drop_tables
    awk 'BEGIN { RS = ";" } /CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode(_link|_path)?`/ { print $0 ";" }' "$SCHEMA" \
        | docker exec -i -e MYSQL_PWD="$(cat "$RUN/.db_root_pass")" "$DB_CONTAINER" mysql -uroot -N -B "$DB"
}

# world.sh stop writes the shutdown command into console.fifo; with no running server nothing reads it and the write blocks
# forever (the run2 trap), so only call it when the container is up.
stop_server() {
    if docker inspect -f '{{.State.Running}}' "$SERVER" 2>/dev/null | grep -q true; then
        ( cd "$RUN" && ./world.sh stop ) >/dev/null 2>&1
    else
        docker rm -f "$SERVER" >/dev/null 2>&1
    fi
    rm -f "$RUN/console.fifo"
}

boot() {
    local tries=${1:-120}
    : > "$LOG"
    : > "$BOTLOG"
    ( cd "$RUN" && ./world.sh start ) >/dev/null 2>&1
    local i
    for i in $(seq 1 "$tries"); do
        grep -q "worldserver-daemon) ready..." "$LOG" && return 0
        docker inspect -f '{{.State.Running}}' "$SERVER" 2>/dev/null | grep -q true || return 1
        sleep 2
    done
    return 1
}

rss() {
    # The worldserver process itself: /proc/<pid>/comm is the executable name (the container's `bash -c` wrapper is not it).
    docker exec "$SERVER" sh -c 'for f in /proc/[0-9]*/comm; do [ "$(cat "$f" 2>/dev/null)" = worldserver ] && awk "/VmRSS/ {print \$2}" "${f%comm}status"; done' 2>/dev/null | head -1
}

common_checks() {
    has "worldserver-daemon\\) ready\\.\\.\\." "server ready"
    local n; n=$(grep -h "ASSERTION FAILED" "$LOG" "$BOTLOG" 2>/dev/null | wc -l)
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
    printf 'scenario b: empty travel tables build the graph, a second boot loads it\n'
    if ! create_empty; then fail "could not create the empty schema"; return; fi
    # The first boot seeds nodes, links them over every map's navmesh, prunes, costs and saves. Turtle took about four
    # minutes for this on a desktop; allow twenty here.
    if ! boot 600; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has 'travel-node dataset is empty; node seeding starts at startup, walk links follow synchronously' "one line for the empty dataset"
    hasnt 'world table `ai_playerbot_travelnode` is missing' "the tables exist, so the guard does not refuse"
    has '\-Generating nodes' "the node seeding ran"
    has 'TortoiseBots: travel walk links started for [0-9]+ nodes \([0-9]+ candidate pairs\)\.' "the walk linking started"
    has 'TortoiseBots: travel walk links done:' "the walk linking finished"
    has 'Calculating mapoffset' "the graph's derived data was built"
    local nodes; nodes=$(sql "SELECT COUNT(*) FROM ai_playerbot_travelnode")
    if [ "${nodes:-0}" -gt 0 ]; then pass "the generated nodes were saved ($nodes rows)"; else fail "no node row was saved"; fi
    local links; links=$(sql "SELECT COUNT(*) FROM ai_playerbot_travelnode_link")
    if [ "${links:-0}" -gt 0 ]; then pass "the generated links were saved ($links rows)"; else fail "no link row was saved"; fi
    stop_server
    # Second boot: the saved graph loads, nothing regenerates.
    if ! boot; then fail "the second boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has '>> Loaded [0-9]+ travelNodes\.' "the saved nodes were loaded"
    hasnt 'travel walk links started' "nothing was regenerated"
    hasnt 'travel-node dataset is empty' "the graph is not empty"
    stop_server
}

scenario_c() {
    printf 'scenario c: a two-continent graph with a zeppelin, a link to a missing node and a walk between two maps\n'
    drop_tables || { fail "could not drop the travel tables"; return; }
    if ! create_empty; then fail "could not create the empty schema"; return; fi
    # Azeroth (map 0): Alpha, Beta, Gamma; Kalimdor (map 1): Kal1, Kal2.
    sql "INSERT INTO ai_playerbot_travelnode (id, name, map_id, x, y, z, linked) VALUES
           (1, 'Alpha', 0, -8949.95, -132.493, 83.5312, 1),
           (2, 'Beta',  0, -8950.0, -130.0,   83.5312, 1),
           (3, 'Gamma', 0, -100.0,  -200.0,   50.0,    1),
           (4, 'Kal1',  1, 1600.0,  -4400.0,  10.0,    1),
           (5, 'Kal2',  1, 1610.0,  -4410.0,  10.0,    1)" || { fail "node insert failed"; return; }
    # 1 -> 424242: no such node row; 2 -> 1: a real walk with three ordered path points; 4 -> 5: a real walk on Kalimdor;
    # 3 -> 4: a zeppelin (type 3, loaded, never a route); 1 -> 4: a "walk" between the two maps (refused at load).
    sql "INSERT INTO ai_playerbot_travelnode_link (node_id, to_node_id, type, object, distance, swim_distance, extra_cost, calculated, max_creature_0, max_creature_1, max_creature_2) VALUES
           (1, 424242, 1, 0, 10.0, 0, 0, 1, 0, 0, 0),
           (2, 1,      1, 0, 20.0, 0, 0, 1, 0, 0, 0),
           (4, 5,      1, 0, 14.0, 0, 0, 1, 0, 0, 0),
           (3, 4,      3, 0, 9000.0, 0, 0, 1, 0, 0, 0),
           (1, 4,      1, 0, 9000.0, 0, 0, 1, 0, 0, 0)" || { fail "link insert failed"; return; }
    sql "INSERT INTO ai_playerbot_travelnode_path (node_id, to_node_id, nr, map_id, x, y, z) VALUES
           (1, 424242, 0, 0, -8949.9, -132.4, 83.5),
           (1, 424242, 1, 0, -8949.9, -132.3, 83.5),
           (2, 1, 0, 0, -8949.95, -132.0, 83.5),
           (2, 1, 1, 0, -8949.97, -132.2, 83.5),
           (2, 1, 2, 0, -8949.95, -132.49, 83.53)" || { fail "path insert failed"; return; }
    if ! boot; then fail "boot did not come up"; tail -5 "$LOG"; return; fi
    common_checks
    has '>> Loaded 5 travelNodes\.' "five nodes loaded"
    has '>> Loaded 5 travelNode paths\.' "all five link rows were read"
    hasnt 'travel-node dataset is empty' "the graph is not empty"
    has 'Calculating mapoffset' "the populated graph builds its derived data"
    # The bad rows are refused at load, counted in one line: the missing target and the walk between two maps.
    has '>> Skipped 1 travelNode links to an unknown node and 1 walk links between two maps\.' "the unknown-node link and the cross-map walk were skipped"
    # What each node reaches over usable links: the zeppelin (Gamma -> Kal1) and the refused walk (Alpha -> Kal1) join nothing, and the
    # link to the missing node is no edge, so no continent reaches the other and the three path points of 2 -> 1 count once.
    has '>> travel graph reach \(usable links\): Alpha=1 Beta=2 Gamma=1 Kal1=2 Kal2=1' "no node reaches across the continents (zeppelin and bad walk are no route)"
    hasnt 'Alpha=[2-9]|Gamma=[2-9]' "Alpha and Gamma reach only themselves"
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

refuse_unless_scratch
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
