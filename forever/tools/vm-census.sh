#!/usr/bin/env bash
# vm-census.sh [only-substring] [jobs]
# Uploads the core and THIS worktree to the build VM and runs the port census there (forever/tools/port_census.py).
#   FOREVER_SLOT=N           use build slot N on the VM (src-N, build-N, census-N.lock): one slot per worktree, so several
#                            rounds can build and measure at the same time. Empty = the first tree.
#   FOREVER_CENSUS_LIST=path a file in this worktree with one source path per line: measure only those (a slice).
#   FOREVER_CENSUS_NAME=name the folder of THIS run (default <worktree>-<slot>-<list or all>-<utc stamp>-<pid>); it lives in
#                            yulon-ubuntu2:~/forever-build/census-runs/<name>/, so two runs never share an output folder.
#   FOREVER_CORE=dir         the core checkout (default ~/ForeverCore).
# Order (it matters): 1. take the slot's lock census[-N].lock on the VM (waits while another run holds it),
# 2. upload the core and the worktree into src[-N], 3. run the census, 4. read the summary, 5. release the lock.
# The lock also covers build[-N].lock, so no ForeverCore build starts on the source while it is being replaced.
# A summary that says "files 0" is not a measurement: the script then fails.
# With a substring only the .cpp files whose path contains it are checked.
# Prints the run folder, summary.txt and the top of where/names/kinds. Full tables: yulon-ubuntu2:~/forever-build/census-runs/<name>/
set -euo pipefail
ONLY=${1:-}; JOBS=${2:-12}; VM=${FOREVER_VM:-yulon-ubuntu2}; CORE=${FOREVER_CORE:-$HOME/ForeverCore}
WT=$(cd "$(dirname "$0")/../.." && pwd); SUF=${FOREVER_SLOT:+-$FOREVER_SLOT}
LIST=${FOREVER_CENSUS_LIST:-}
NAME=${FOREVER_CENSUS_NAME:-$(basename "$WT")-s${FOREVER_SLOT:-0}-$( [ -n "$LIST" ] && basename "$LIST" .txt || echo all )-$(date -u +%Y%m%dT%H%M%SZ)-$$}
case "$NAME" in *[!A-Za-z0-9._-]*|"") echo "vm-census.sh: FOREVER_CENSUS_NAME may only hold letters, digits, . _ -" >&2; exit 2;; esac
OUT=census-runs/$NAME

# 1. The lock first. A background ssh session holds census[-N].lock (and build[-N].lock) and ends, releasing both, when this
# script closes its stdin, however this script ends.
coproc HOLD { ssh "$VM" "mkdir -p \$HOME/forever-build/census-runs; exec 8>\$HOME/forever-build/census$SUF.lock 9>\$HOME/forever-build/build$SUF.lock; flock 8; flock 9; echo LOCKED; cat >/dev/null"; }
release() { eval "exec ${HOLD[1]}>&-" 2>/dev/null || true; wait "$HOLD_PID" 2>/dev/null || true; }
trap release EXIT
echo "vm-census: waiting for the slot ${FOREVER_SLOT:-0} lock ($NAME)" >&2
read -r -u "${HOLD[0]}" got || { echo "vm-census: could not take the slot lock on $VM" >&2; exit 1; }
[ "$got" = LOCKED ] || { echo "vm-census: unexpected answer from the lock session: $got" >&2; exit 1; }
echo "vm-census: lock held, uploading" >&2

# 2. Only now replace the slot's source. The core without its modules link, then this worktree as the module (so a slot never
# depends on where the core's src/modules/TortoiseBots link points).
rsync -a --delete --exclude .git --exclude 'src/modules/TortoiseBots' "$CORE/" "$VM:forever-build/src$SUF/"
rsync -a --delete --exclude .git "$WT/" "$VM:forever-build/src$SUF/src/modules/TortoiseBots/"

# 3 + 4. The census of THIS source into this run's own folder, and the summary read from that folder (the lock is still ours).
# The runner is the one in the tree just uploaded.
ssh "$VM" "FOREVER_SLOT='${FOREVER_SLOT:-}' FOREVER_CENSUS_LIST='$LIST' FOREVER_CENSUS_OUT='$OUT' ~/forever-build/src$SUF/src/modules/TortoiseBots/forever/tools/census.sh $JOBS '$ONLY' 2>&1 | tail -3
cd ~/forever-build/$OUT && echo 'run folder: ~/forever-build/$OUT' && cat summary.txt && case \"\$(cat summary.txt)\" in 'files 0 '*) echo 'vm-census: files 0, not a measurement' >&2; exit 3;; esac
echo '--- where' && head -15 where.tsv && echo '--- names' && head -40 names.tsv && echo '--- kinds' && head -12 kinds.tsv | cut -c1-140"
