# Wailing Caverns end (Anacondra pool, Disciple gate, Mutanus)

Client checks: 2026-11-19 session 20. Data: TDB 2026-10-14 + client DB2 70959.

## Lady Anacondra spawn 20027366 vs Druid of the Fang 20301002

- `pool_template` **10004 "Lady Anacondra", max_limit=1**. Its `pool_members` are four Anacondra
  spawn points: **20027366**, 20300999, 20301000, 20301001. Only one is active at a time, so the
  prefill's "4 spawns" were pool members, not four simultaneous bosses.
- Druid of the Fang **20301002** is **not in the pool**. It is a separate static spawn at
  `15.2579 297.2160 -87.7785` and is the **leader of a formation** with Deviate Guardians 20027359 and
  20027360 (entry 3637; `groupAI=3`, dist 0).
- In game the spot alternated: first only the Druid was visible, later Lady Anacondra (level 20) was
  visible with Deviate Guardians, while `.npc near` still listed Anacondra 20027366 + Druid 20301002 +
  Guardian 20027360. This is exactly what a one-at-a-time pool plus the extra Druid formation looks like.
- `.go xyz 15.3449 297.1760 -87.7016 43`, then `.npc near`.

## Disciple of Naralex gossip gate / escort

- Disciple 3678 has `ScriptName=classic_npc_disciple_of_naralex` (EscortAI); spawn 20018675 at
  `-134.965 125.402 -78.0945`.
- With the four Fanglords alive, gossip says: "Only after his corrupted servants, the Fanglords, have
  been slain can I perform the awakening ritual." No start option.
- After GM-killing Anacondra, Cobrahn, Pythas and Serpentis, the Disciple **did start the escort** and the
  event ran. Audrey then GM-killed the Disciple mid-escort; retry after `.respawn` near him.
- `.go xyz -134.965 125.402 -78.0945 43`.

## Mutanus

- Not verified: the Disciple was killed by accident before Mutanus.
- The classic script's subevent 6 calls `SummonAttacker(MOB_MUTANUS_DEVOURER, ClassicDiscipleSummonPos[3])`
  = **Mutanus 3654 at 142.7 254.0 -102.2 map 43**.
- `creature_summon_groups` also has a Mutanus row at `150.943 262.797 -103.904`, but the custom
  `classic_npc_disciple_of_naralex` script is what runs; use the script position.
- Naralex 3679 is friendly faction 35, `unit_flags=512` IMMUNE_TO_NPC, flags_extra CIVILIAN + NO_XP,
  ReactorAI, no script name, spawn 2h at `115.408 240.017 -94.021`.
- Test: `.go xyz 142.7 254.0 -102.2 43`, finish the escort, then `.npc near`; Mutanus should appear at
  that spot. `.go xyz 115.408 240.017 -94.021 43` for Naralex.
