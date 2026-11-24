---
id: developers-donor-hierarchy
title: Donor Hierarchy & Porting Rules
category: developers
summary: Evidence priority and architectural guidelines when referencing mod-playerbots and ForeverCore.
tags: [donors, mod-playerbots, porting, rules]
relates_to:
  - developers-architecture
  - developers-strategy-engine
---

# Donor Hierarchy & Porting Rules

When implementing or diagnosing behavior in TortoiseBots, do not copy a donor wholesale. Different sources answer different questions.

## Evidence hierarchy

Evaluate evidence in this order:

1. **The client and server at runtime** — ground truth. If it behaves differently from any document, the runtime wins.
2. **ForeverCore at the pinned commit** (`forever/PINNED_CORE`) — engine lifecycle, session behaviour, script hooks, and the server's own spell, item and creature data (DB2 stores, world database).
3. **mod-playerbots** ([mod-playerbots/mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)) — the primary mature behaviour donor for combat rotations, healing priorities, interrupts, crowd control, dungeon play and tactics. Its code is a reference for behaviour, not a template for structure.

Turtle-era material (Tortoise WoW 1.18.1 custom spells, races, items, zones and dungeons) is not evidence on Forever unless the same ID exists in the Forever data. When in doubt, query the core's stores instead of quoting an old doc.

## Porting rules

```text
Forever runtime mechanics  -> ForeverCore first
Mature gameplay behavior   -> mod-playerbots first
Architecture ownership     -> TortoiseBots rules always win
```

### Harvest behavior, never coupling

- Reimplement the behaviour inside the module; never vendor a donor tree.
- Never copy global bot managers, baked-in player pointers, donor threads or core-level bot checks.
- Keep bot state and decisions in the module: `forever/src` plus the legacy `ai/playerbot` files the slice build compiles.
- A new capability that the core lacks is a module policy or a generic core seam proposal, not a donor API pasted in.
- Expand only what the behaviour needs: donor expansion-only content (TBC/WotLK spells, talents, items, zones) is not ported, and neither is Turtle-only custom content.

### Record provenance

- Add each donor-derived behaviour to [data/donor-map.tsv](data/donor-map.tsv): donor repository and commit, the file or symbol read, the local owner, and whether the behaviour was kept, adapted or rejected.
- Name the donor repository, commit and the behavior in the pull request description.
