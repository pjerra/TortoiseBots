#!/usr/bin/env bash
# vm-census.sh [only-substring] [jobs]
# Uploads the core and THIS worktree to the build VM and runs the port census there (forever/tools/port_census.py).
#   FOREVER_SLOT=N           use build slot N on the VM (src-N, build-N, census-N.lock): one slot per worktree, so several
#                            rounds can build and measure at the same time. Empty = the first tree.
#   FOREVER_CENSUS_LIST=path a file in this worktree with one source path per line: measure only those (a slice).
#   FOREVER_CENSUS_NAME=name the folder of THIS run (default <worktree>-s<slot>-<list or all>-<utc stamp>-<pid>); it lives in
#                            yulon-ubuntu2:~/forever-build/census-runs/<name>/, so two runs never share an output folder.
#   FOREVER_CORE=dir         the core checkout (default ~/ForeverCore).
# Order (it matters): 1. take the slot's lock census[-N].lock on the VM (waits while another run holds it),
# 2. upload the core and the worktree into src[-N], 3. run the census, 4. read the summary, 5. release the lock.
# The lock also covers build[-N].lock, so no ForeverCore build starts on the source while it is being replaced.
# The VM-side session that takes the lock also RUNS the census and reads the summary, so the census process holds the lock
# itself: if this script or its ssh connection dies, the census on the VM goes on and the next run waits for it.
# A summary that says "files 0" is not a measurement: the script then fails (exit 3). Exit 1: the VM session was lost.
# With a substring only the .cpp files whose path contains it are checked.
# Prints the run folder, summary.txt and the top of where/names/kinds. Full tables: yulon-ubuntu2:~/forever-build/census-runs/<name>/
set -euo pipefail
ONLY=${1:-}; JOBS=${2:-12}; VM=${FOREVER_VM:-yulon-ubuntu2}; CORE=${FOREVER_CORE:-$HOME/ForeverCore}
WT=$(cd "$(dirname "$0")/../.." && pwd); SUF=${FOREVER_SLOT:+-$FOREVER_SLOT}
LIST=${FOREVER_CENSUS_LIST:-}
NAME=${FOREVER_CENSUS_NAME:-$(basename "$WT")-s${FOREVER_SLOT:-0}-$( [ -n "$LIST" ] && basename "$LIST" .txt || echo all )-$(date -u +%Y%m%dT%H%M%SZ)-$$}
case "$NAME" in *[!A-Za-z0-9._-]*|"") echo "vm-census.sh: FOREVER_CENSUS_NAME may only hold letters, digits, . _ -" >&2; exit 2;; esac
OUT=census-runs/$NAME

# 1. The lock first, in one VM-side session that stays for the whole run. It prints LOCKED, waits for GO (sent after the
# upload; stdin closing instead ends it and frees the locks), then runs the census of the uploaded tree and reads the summary.
# The census is a child of this session and inherits the lock descriptors, so it keeps the slot locked even if the session dies.
coproc VM_SESSION { ssh "$VM" "exec 2>&1; mkdir -p \$HOME/forever-build/census-runs; exec 8>\$HOME/forever-build/census$SUF.lock 9>\$HOME/forever-build/build$SUF.lock; flock 8; flock 9; echo LOCKED
read -r go || exit 1; [ \"\$go\" = GO ] || exit 1
FOREVER_SLOT='${FOREVER_SLOT:-}' FOREVER_CENSUS_LIST='$LIST' FOREVER_CENSUS_OUT='$OUT' \$HOME/forever-build/src$SUF/src/modules/TortoiseBots/forever/tools/census.sh $JOBS '$ONLY' >\$HOME/forever-build/census-runs/$NAME.census.log 2>&1
census_rc=\$?
tail -3 \$HOME/forever-build/census-runs/$NAME.census.log
if [ \$census_rc -ne 0 ]; then echo \"vm-census: the census failed (rc \$census_rc; log ~/forever-build/census-runs/$NAME.census.log)\"; echo VM-CENSUS-RC=\$census_rc; exit 0; fi
cd \$HOME/forever-build/$OUT || { echo \"vm-census: the census wrote no run folder ~/forever-build/$OUT\"; echo 'VM-CENSUS-RC=1'; exit 0; }
[ -s summary.txt ] || { echo 'vm-census: the census wrote no summary.txt'; echo 'VM-CENSUS-RC=1'; exit 0; }
for f in where.tsv names.tsv kinds.tsv; do [ -e \$f ] || { echo \"vm-census: the census wrote no \$f\"; echo 'VM-CENSUS-RC=1'; exit 0; }; done
echo 'run folder: ~/forever-build/$OUT'; cat summary.txt
case \"\$(cat summary.txt)\" in 'files 0 '*) echo 'vm-census: files 0, not a measurement'; echo 'VM-CENSUS-RC=3'; exit 0;; esac
echo '--- where'; head -15 where.tsv; echo '--- names'; head -40 names.tsv; echo '--- kinds'; head -12 kinds.tsv | cut -c1-140
echo 'VM-CENSUS-RC=0'"; }
SIN=${VM_SESSION[0]}; SOUT=${VM_SESSION[1]}
release() { set +e; [ -n "${SOUT:-}" ] && eval "exec ${SOUT}>&-" 2>/dev/null; [ -n "${VM_SESSION_PID:-}" ] && wait "$VM_SESSION_PID" 2>/dev/null; return 0; }
trap release EXIT
echo "vm-census: waiting for the slot ${FOREVER_SLOT:-0} lock ($NAME)" >&2
read -r -u "$SIN" got || { echo "vm-census: could not take the slot lock on $VM" >&2; exit 1; }
[ "$got" = LOCKED ] || { echo "vm-census: unexpected answer from the VM session: $got" >&2; exit 1; }
echo "vm-census: lock held, uploading" >&2

# 2. Only now replace the slot's source. The core without its modules link, then this worktree as the module (so a slot never
# depends on where the core's src/modules/TortoiseBots link points).
rsync -a --delete --exclude .git --exclude 'src/modules/TortoiseBots' "$CORE/" "$VM:forever-build/src$SUF/"
rsync -a --delete --exclude .git "$WT/" "$VM:forever-build/src$SUF/src/modules/TortoiseBots/"

# 3 + 4. The VM session runs the census of THIS source into this run's own folder and reads the summary (the lock is still ours).
echo GO >&"$SOUT" || { echo "vm-census: the VM session ended before the census started" >&2; exit 1; }
rc=""
while IFS= read -r line <&"$SIN"; do
    case "$line" in
        VM-CENSUS-RC=*) rc=${line#VM-CENSUS-RC=};;
        "vm-census: "*) echo "$line" >&2;;
        *) printf '%s\n' "$line";;
    esac
done
[ -n "$rc" ] || { echo "vm-census: the VM session ended before the census finished (the census on the VM goes on and keeps the slot locked until it ends; its folder: ~/forever-build/$OUT)" >&2; exit 1; }
exit "$rc"
