# Shaman — review against WoW Forever 70959

Builds reviewed: `forever/data/waterys/talent-builds.csv` against 70959 `Talent.db2`/`SpellName.db2`,
plus the shaman strategy files. Rank 2+ talent spells are absent from the client DB in every build (known
limitation); only rank-1 gaps are called out.

## Builds

| Spec | Points | Status | Build issues |
|---|---:|---|---|
| Elemental | 51/51 | all talent IDs exist; no rank > max | 1 position-custom, 2 overlaps; Fire Nova action needs a live check |
| Enhancement | 51/51 | all talent IDs exist; no rank > max | 2 position-custom, 2 row-custom, 1 overlap |
| Restoration | 51/51 | all talent IDs exist; no rank > max | 4 position-custom, 1 row-custom, 2 overlaps; rank-1 gap talent 562 |
| Tank (custom) | 51/51 | all talent IDs exist; no rank > max | 5 position-custom, 1 overlap; this is the Turtle custom tank kit, not a Forever tree |

No missing talent IDs, no cumulative rank > max, and no final 5-points-per-tier violation.

### Client SpellName gaps

- Talent **562** rank-1 spell **16041** is absent from the 70959 client DB; the same talent is used by
  Elemental/Restoration. Verify live.

## Rotation / spells

| Spell / action | 70959 | Effect |
|---|---|---|
| Fire Nova totem | 1535 **absent**; 408341-408345 **Fire Nova exists** | `CastFireNovaAction` still passes the name `"fire nova totem"` (`ShamanActions.h:484`); action likely dead unless SpellMgr aliases it. The class note calls for 408341. |
| Lava Burst | 408490 **exists** | new Forever spell; use it where the rotation expects it |
| Earth Shield | 974/408514 **exists** | class note's "missing" is stale; action should resolve |
| Water Shield | 408510/408511 **exists** | class note's "missing" is stale |
| Tranquil Air | 25909 **Tranquil Air** exists; 25908 absent | action name is `tranquil air totem`; exact-name check needed |
| Ancestral Swiftness | **absent** | Restoration action/trigger dead |
| Earthshaker Slam | **absent by exact name** (Earthshaker 21152 exists) | action dead/name-sensitive |
| Totemic Recall | 36936/39104 **exists** | class note's "missing" is stale; action should resolve |

## Dead conditions and custom differences

- Fire Nova's totem name is the highest-risk dead action; Earth Shield/Water Shield/Totemic Recall are
  present and should not be listed as missing.
- Tranquil Air 25908 in the class note is off by one: 25909 is the named spell; the `tranquil air totem`
  action still needs its exact-name check.
- The **Tank** spec is a Turtle custom kit (Elemental + Enhancement points), not a Forever talent tree;
  it has no special Forever tree to review past the same IDs.
- Custom Turtle talents with no exact counterpart (from `talents-detail.csv`): Elemental 223/232/234,
  Enhancement 223/257/258/266, Restoration 223/245/246/248/249/250, Tank 223/253/258/259/261/266.
  Position/row-custom mappings are not exact spell maps.

## Rotation spell check (70959 `SpellName`)

| Spec | Spell | Known |
|---|---|---|
| Elemental | Lightning Bolt, Chain Lightning, Earth Shock, Lava Burst 408490 | yes |
| Elemental/Enhancement | Fire Nova (1535 absent; 408341 exists) | changed; `CastFireNovaAction` still uses `fire nova totem` name — likely dead |
| Enhancement | Stormstrike/Windfury Totem (8512) | yes by name |
| Restoration | Healing Wave, Chain Heal, Earth Shield (974/408514), Water Shield (408510) | yes — old “missing” notes stale |
| Restoration | Ancestral Swiftness, Earthshaker Slam (exact), Tranquil Air Totem action | no exact action name; Tranquil Air 25909 exists, Earthshaker 21152 exists |
