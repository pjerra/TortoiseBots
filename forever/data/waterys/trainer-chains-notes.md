# Trainer chain check (poisons, Purge, Windfury, hunter pets)

`forever/data/waterys/trainer-chains-check.csv` (38 rows). Sources: TDB `trainer_spell`
(34696 rows), 70888 `SkillLineAbility.db2` (parsed field-by-field: skill/minrank/acquire),
70959 `SpellName.db2` presence, `quest_template` RewardSpell + display cols 13-16 (no hits
for any checked spell), `playercreateinfo_action` starting bars.

## Results

- **Purge: COMPLETE.** Rank 1 (370) @12, rank 2 (8012) @32, both trainer-taught (ReqAbility chain 370->8012). SLA acquire default=Learned. Nothing missing.
- **Windfury Weapon: rank 2 missing.** Rank 1 (8232) @30, rank 3 (10486) @50, rank 4 (16362) @60 all taught. Rank 2 (8233, in SpellName.db2) has NO trainer rows.
- **Instant Poison: ranks I-III missing, VII unobtainable.** Trainers teach IV (8926) @22 <requires 8925>, V (8927) @28, VI (8928) @34. I (6947), II (6949), III (6950) have NO trainer rows, NO SLA rows, NO quest. VII (21927, TBC-era) the same. (8925: taught @16, prereq for IV.)
- **Deadly Poison: I, III-V missing; I and V absent from client data.** II (2893) @26 taught. I (2892): NO trainer, NO SLA row, NOT in 70959 SpellName.db2. III (8984) and IV (8985): in SpellName but NO trainer rows. V (20844): no trainer + NOT in SpellName. VI/VII (22053/22054, TBC-era): no trainer. (Deadly I/II are also sold by Ruppo Zipcoil, entry 2688 - the only vendor-sold poison ranks found.)
- **Crippling Poison: I missing; II on the wrong trainer type.** I (3775): nothing. II (3776): taught, but only by leatherworking-type trainers (56 with ReqSkillLine 2532@180; 1003007/1004212/1011097/1011098 with 165=Leatherworking@180, ReqLevel 0) - flagged in the CSV, verify intended vs data quirk.
- **Mind-numbing / Wound Poison: entirely missing.** 5237/6951/9186 and 10918/10920/10921/10922 have NO trainer rows and NO quests. 9186, 10918, 10920, 10922 are also absent from 70959 SpellName.db2 (only 10921 exists). No SLA acquire rows for any of them.
- **Hunter chain: Call Pet, Revive Pet, Beast Training missing.** Tame Beast 1515 (@13), Dismiss 2641 (@10), Feed Pet 6991 (@13), Mend Pet 136 (@12/14), Eyes of the Beast 1002 (@14) taught. Call Pet 883, Revive Pet 982, Beast Training 5149: NO trainer rows; quest 94013 "Taming the Beast" teaches 1515+883+2641 via display spells (FB's class_quest_spells mechanism), but nothing found for 982/5149.

## Verdict: data thin, not a rank rule

Taught ranks are scattered (instant IV-VI but not I-III; deadly II only; windfury 1/3/4 but not 2),
which fits partial/hand-built trainer data, not a rule (no even/odd or tier pattern holds).
Low ranks are NOT auto-learned either (no SLA acquire=2 rows for any of them; Schl-around:
78/2457-style auto-learn does not apply). The bot impact: poisons above what trainers teach
can't be (re)applied; warrior stance/taunt/sunder (round 8) is the same gap class.
Suggested fix for Karrz: add the missing `trainer_spell` rows (copy ReqLevel/ReqAbility pattern
from the taught neighbors), or confirm the intended alternate path.
