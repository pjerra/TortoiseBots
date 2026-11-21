# Warrior notes for RFC (Ragefire Chasm, level 18, 1 prot + 1 priest + 3 arms)

Client: DB2 70938 (db2-70938 extraction 2026-11-18) for spell names/ids (all RFC ids verified, see rfc-70938-check.csv); trainer/starting-bar evidence from TDB 2026-10-14 + 70888 SkillLineAbility parse. Target client is 70938.

## CONFIRMED: Defensive Stance (71), Taunt (355), Sunder Armor (7386) have NO learn path on Forever as-shipped

Confirmed: a fresh warrior knows only Heroic Strike + Battle Stance, and there is no trainer
for the other three. Evidence, core data first:

- `trainer_spell` (TDB world, 34696 rows): zero rows for 71, 355, 7386. (Battle Shout 6673,
  Charge 100, Rend 772, Thunder Clap 6343, Bloodrage 2687, Shield Bash 72, Overpower 7384,
  Demoralizing Shout 1160, Revenge 6572, Shield Block 2565, Mocking Blow 694 all have rows
  with ReqLevel 1/4/4/6/10/12/12/14/14/16/16. Defensive Stance/Taunt/Sunder have none.)
- `SkillLineAbility.db2` (70888, parsed field-by-field): 71 = ID 6101, 355 = ID 6114,
  7386 = ID 6109, all skill 257 (Protection), min rank 1, AcquireMethod ABSENT (= default
  Learned/trainer). By contrast 78/2457 carry explicit acquire 2 (AutomaticCharLevel) - that
  is why the fresh warrior knows them and nothing else.
- Quest reward: no `quest_template` row has these as RewardSpell.
- Action bars: `playercreateinfo_action` gives the warrior only 6603 (attack) + 78 + custom
  buffs at creation.

So as-shipped the level-10/12 prot toolkit is unobtainable. This is a data gap, not a level
question: see `guild-order-recipes.csv` style fix - Karrz needs `trainer_spell` rows for
71/355/7386 (suggested: same trainers/ReqLevels as the neighboring warrior spells), or the
port must grant them another way. Until then: the RFC prot warrior tanks in Battle Stance
with Shout/Thunder Clap/Revenge/Shield Block, no Taunt (Mocking Blow 694 at 16 is the only
forced attack), no Sunder (Rend + Heroic Strike threat only).

## What the RFC warrior HAS by 18 (warrior-spells-1-18.csv)

1: Heroic Strike (starting+auto), Battle Stance (auto), Battle Shout (trainer).
4: Charge, Rend. 6: Thunder Clap. 10: Bloodrage. 12: Shield Bash, Overpower.
14: Demoralizing Shout, Revenge. 16: Shield Block, Mocking Blow. 18: nothing new.
Ranks are all rank 1 in this bracket (rank-2+ starts at 20+ for most lines).

Note (DeepSeek 70938 check): spell 5019 is named `Shoot (wand)` in the priest/mage CSVs vs DB2 `Shoot`; id present in both builds, annotation kept.

## Mocking Blow 694 (the level-16 forced attack)

- How learned: trainer only. `trainer_spell`: 26 rows, TrainerIds 1000913+ (warrior trainers), cost 2000 copper, ReqLevel 16, no ReqSkill/ReqAbility gates. Single rank (no higher-rank spell id; 70938 SpellName `Mocking Blow` matches).
- Ranks: 1. Cooldown 2 min in classic; single-target forced attack for 6 s (taunt effect + damage). At 18 it is the ONLY forced attack available (Taunt 355 has no learn path - see above).

## What a good tank does when a second mob turns (level 18 kit only)

No Taunt, no Sunder, no Defensive Stance. Work with what exists:

1. **Mocking Blow (694)** the turned mob if it is in melee range and the 2-min cooldown is up - the only hard redirect. Save it for healer aggro, not for a DPS's mob.
2. **Challenging Shout is NOT available** (no learn path either - same gap class as Taunt; not in the 1-18 CSV for that reason). Do not plan around it.
3. **Thunder Clap (6343)** for AoE threat when 2+ mobs are on the group; **Demoralizing Shout (1160)** lowers their damage while you rebuild.
4. **Rend (772) + Heroic Strike (78)** tab-targeted onto the loose mob: Rend's bleed ticks hold a trickle of threat while you rebuild on it.
5. **Shield Block (2565)** before the loose mob reaches you - the block value converts to threat on Shield Slam? No Shield Slam without the stance tree - Block still cuts incoming damage while the healer catches up.
6. **Overpower (7384)** only helps on the mob already targeting you (dodge proc); do not chase procs on the wrong target.
7. **Healer notes**: priest pre-shields (Power Word: Shield 17) the warrior before pulls; Renew (139) ticks instead of big Heal casts to avoid spike aggro; Fade (586) exists at 12 if the priest pulls.
8. **CC notes**: mage Polymorph (118, level 8) takes one humanoid/beast out of the fight entirely - sheep the second mob BEFORE it turns, not after. Rogue Sap (6770, level 10) does the same pre-pull. At 18 both are trained and available; plan the pull around them, not around Taunt.
