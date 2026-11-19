#!/usr/bin/env bash
# facts_rfc.sh <worktree> <plan> <outdir> [ticks]
# R0 of slice S5: build forever/tools/facts_trace.cpp (the access tracer of the run leader's `Facts`), run it on the plan's
# exported rows under every scenario, and symbolize the read sites. Heavy: run it on the build VM, not on the laptop.
#   forever/tools/facts_rfc_report.py <outdir> then writes the field list (forever/docs/RUN-S5-FACTS-RFC.md).
set -euo pipefail
WT=$(cd "$1" && pwd); PLAN=$2; OUT=$3; TICKS=${4:-3000}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
python3 "$WT/forever/tools/facts_fields.py" --inc "$OUT/fields.inc"
python3 "$WT/forever/tools/facts_rfc_rows.py" "$PLAN" "$OUT/rows.tsv"
nice -n 10 g++ -std=c++17 -O0 -g -no-pie -fno-omit-frame-pointer -DFIELDS_INC="\"$OUT/fields.inc\"" \
    "$WT/forever/tools/facts_trace.cpp" -o "$OUT/facts_trace"
for scn in happy rest death wipe straggler patrol evade; do
    FACTS_SCN=$scn "$OUT/facts_trace" "$OUT/rows.tsv" "$OUT/fields.inc" "$OUT/out-$scn.tsv" "$TICKS" 2> "$OUT/log-$scn.txt" || true
done
# every address the tracer recorded, once; addr2line prints "addr", function, file:line per address
cat "$OUT"/out-*.tsv | grep -v '^#' | awk -F'\t' '{for (i = 7; i <= 12; i++) if ($i != "0") print $i}' | sort -u > "$OUT/addrs.txt"
addr2line -e "$OUT/facts_trace" -f -C -a < "$OUT/addrs.txt" | paste - - - > "$OUT/syms.tsv"
# line coverage of the same scenarios (a second build): which lines of the policy headers ran, so the report can tell a read site that is
# gated off in the simulation from one in code the rows never reach
mkdir -p "$OUT/cov"
nice -n 10 g++ -std=c++17 -O0 -g -no-pie -fno-omit-frame-pointer --coverage -DFIELDS_INC="\"$OUT/fields.inc\"" \
    "$WT/forever/tools/facts_trace.cpp" -o "$OUT/cov/facts_trace"
for scn in happy rest death wipe straggler patrol evade; do
    FACTS_SCN=$scn "$OUT/cov/facts_trace" "$OUT/rows.tsv" "$OUT/fields.inc" "$OUT/cov/out-$scn.tsv" "$TICKS" 2> /dev/null || true
done
( cd "$OUT/cov" && gcov -o . facts_trace.gcda > gcov.log 2>&1 || true )
echo "scenarios done: $(ls "$OUT"/out-*.tsv | wc -l), addresses $(wc -l < "$OUT/addrs.txt")"
