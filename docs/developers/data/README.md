---
id: developers-data
title: Developer Reference Data
category: developers
summary: Tab-separated reference tables for the Forever build's capabilities, donor provenance, spell coverage, and talent builds.
tags: [developer, data, tsv, reference]
---

# Developer Reference Data

Tab-separated reference tables (`.tsv`). People read them; the build does not. Each file is a ledger with its own columns.

| File | Content | State |
| :--- | :--- | :--- |
| `capabilities.tsv` | Forever-build capability inventory: area, what it does, whether it is in the build, the owner files, and the check that proves it. | Current for the Forever build. |
| `donor-map.tsv` | Donor provenance: repository and commit, the area read, the local owner, keep/adapt/reject, and whether the local code is in the Forever build. | Current for the Forever build. |
| `spell-coverage.tsv` | Per-class spell catalogue from the Turtle-era audit (spell ids, acquisition, action/trigger mapping). | Reference only: not regenerated for ForeverCore data. |
| `talent-builds.tsv` | Turtle-era 1.12 talent presets and learning orders. | Reference only: Forever bots spend talents along `forever/data/waterys/talent-builds.csv` (the level-60 picks of Forever players, spent in file order). |

When a capability or donor row changes, update the table in the same change. When the Forever data set or the engine registry changes, the first two tables are the ones to re-check; `spell-coverage.tsv` is still a Turtle-era ledger whose Forever rows are corrected only where the engine casts something different.
