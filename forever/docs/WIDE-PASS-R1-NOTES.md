# Wide mechanical pass R1: existing helpers onto the files outside the slices

Branch `port/wide-pass` (off `port/forever`). Worktree `~/wt-wide-pass`. Measure: whole tree
`forever/tools/vm-census.sh "" 10` (default slot 0 on the build VM).

Scope: every `.cpp` outside `forever/` and outside `forever/SLICE1.txt` (133 files), `SLICE2.txt` (160) and `SLICE3.txt`
(170), plus their same-base headers. The exclusion list is `forever/tools/wide-pass-exclude.txt` (335 paths, generated from
the three lists on 2026-11-19).

Rules applied: only helpers that already exist, only where the Turtle body is the original expression (identity), one call in,
one call out, no new helper, no `#if` on the core outside `forever/`, no behaviour change on Turtle.

## Baseline

| Run | Command | Result |
|---|---|---|
| whole tree, baseline | `forever/tools/vm-census.sh "" 10` | see "Numbers" |

## Families

(filled per family as they are closed: pattern, helper, places, script, what was checked by eye)

## Left for the slices

(filled as families stop: family, count, three example file:line places)
