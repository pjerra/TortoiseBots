#!/usr/bin/env bash
# census.sh [jobs] [only-substring]
# The VM-side half of vm-census.sh: runs port_census.py in the forever-builder image on src[-N], writing the tables into
# the folder named by FOREVER_CENSUS_OUT (under ~/forever-build, one folder per RUN, never a bare slot number).
#   FOREVER_SLOT=N           source slot (src-N, build-N); empty = the first tree
#   FOREVER_CENSUS_OUT=rel   output folder under ~/forever-build (default census-runs/manual-<utc stamp>-<pid>)
#   FOREVER_CENSUS_LIST, FOREVER_CENSUS_DEFINES   as for port_census.py
# vm-census.sh holds the slot lock around the upload, this script and the read of the summary: do not run it without that.
set -uo pipefail
SUF=${FOREVER_SLOT:+-$FOREVER_SLOT}
OUT=${FOREVER_CENSUS_OUT:-census-runs/manual-$(date -u +%Y%m%dT%H%M%SZ)-$$}
case "$OUT" in /*|*..*) echo "census.sh: FOREVER_CENSUS_OUT must be a relative folder without .." >&2; exit 2;; esac
cd ~/forever-build && rm -rf "$OUT" && mkdir -p "$OUT" "build$SUF"
docker run --rm --cpus=${1:-12} -u "$(id -u):$(id -g)" -e HOME=/tmp -e FOREVER_CENSUS_LIST="${FOREVER_CENSUS_LIST:-}" -e FOREVER_CENSUS_DEFINES="${FOREVER_CENSUS_DEFINES:-}" -v "$PWD/src$SUF:/src:ro" -v "$PWD/build$SUF:/build" -v "$PWD/$OUT:/census" forever-builder \
  bash -c "nice -n 10 python3 /src/src/modules/TortoiseBots/forever/tools/port_census.py /build /census ${1:-12} ${2:-}"
rc=$?
rm -f "$OUT/botpch_forever.pch"    # 120 MB per run, only needed while the census runs
[ $rc -eq 0 ] || exit $rc
# A run that wrote no summary is not a measurement: say so instead of letting the caller read an empty folder.
[ -s "$OUT/summary.txt" ] || { echo "census.sh: no summary.txt in $OUT: the census did not finish" >&2; exit 3; }
exit 0
