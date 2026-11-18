#!/usr/bin/env bash
# vm-census.sh [only-substring] [jobs]
# Uploads the core and THIS worktree to the build VM and runs the port census there (forever/tools/port_census.py).
#   FOREVER_SLOT=N           use build slot N on the VM (src-N, build-N, census-N): one slot per worktree, so several
#                            rounds can build and measure at the same time. Empty = the first tree.
#   FOREVER_CENSUS_LIST=path a file in this worktree with one source path per line: measure only those (a slice).
#   FOREVER_CORE=dir         the core checkout (default ~/ForeverCore).
# With a substring only the .cpp files whose path contains it are checked.
# Prints summary.txt and the top of where/names/kinds. Full tables: yulon-ubuntu2:~/forever-build/census[-N]/
set -euo pipefail
ONLY=${1:-}; JOBS=${2:-12}; VM=${FOREVER_VM:-yulon-ubuntu2}; CORE=${FOREVER_CORE:-$HOME/ForeverCore}
WT=$(cd "$(dirname "$0")/../.." && pwd); SUF=${FOREVER_SLOT:+-$FOREVER_SLOT}
# The core without its modules link, then this worktree as the module (so a slot never depends on where the core's
# src/modules/TortoiseBots link points).
rsync -a --delete --exclude .git --exclude 'src/modules/TortoiseBots' "$CORE/" "$VM:forever-build/src$SUF/"
rsync -a --delete --exclude .git "$WT/" "$VM:forever-build/src$SUF/src/modules/TortoiseBots/"
ssh "$VM" "FOREVER_SLOT='${FOREVER_SLOT:-}' FOREVER_CENSUS_LIST='${FOREVER_CENSUS_LIST:-}' flock ~/forever-build/census$SUF.lock ~/forever-build/census.sh $JOBS '$ONLY' 2>&1 | tail -3; cd ~/forever-build/census$SUF && cat summary.txt && echo '--- where' && head -15 where.tsv && echo '--- names' && head -40 names.tsv && echo '--- kinds' && head -12 kinds.tsv | cut -c1-140"
