#!/usr/bin/env bash
# vm-census.sh [only-substring] [jobs]
# Uploads this worktree to the build VM and runs the port census there (forever/tools/port_census.py).
# With a substring only the .cpp files whose path contains it are checked (minutes instead of most of an hour).
# Prints summary.txt and the top of where/names/kinds. Full tables: yulon-ubuntu2:~/forever-build/census/
set -euo pipefail
ONLY=${1:-}; JOBS=${2:-12}; VM=${FOREVER_VM:-yulon-ubuntu2}; CORE=${FOREVER_CORE:-$HOME/ForeverCore}
rsync -aL --delete --exclude .git --exclude 'src/modules/TortoiseBots/.git' "$CORE/" "$VM:forever-build/src/"
ssh "$VM" "flock ~/forever-build/census.lock ~/forever-build/census.sh $JOBS '$ONLY' >/dev/null 2>&1; cd ~/forever-build/census && cat summary.txt && echo '--- where' && head -15 where.tsv && echo '--- names' && head -40 names.tsv && echo '--- kinds' && head -12 kinds.tsv | cut -c1-140"
