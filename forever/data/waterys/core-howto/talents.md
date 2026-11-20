# Talents — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root.

There are **two** talent paths at this commit and the 1.60 client uses both: the trait-config tabs
(`C_ClassTalents`, the modern tree UI) and the legacy `Talent.dbc` purchase path (`CMSG_LEARN_TALENTS`).

## A. Talent trees as trait configs (what the client's talent tab edits)

1. **Open the talent tab**. Nothing is requested: the committed configs are update fields
   (`UF::TraitConfig`, `m_activePlayerData->TraitConfigs`), so the client already has them and edits a
   local copy.
2. **Apply / commit points on the tree**. `CMSG_TRAITS_COMMIT_CONFIG` (`Opcodes.cpp:1133`) →
   `HandleTraitsCommitConfig` (`Handlers/TraitHandler.cpp:27`): refuses an unknown config with
   `TraitConfigCommitFailed` / `TALENT_FAILED_UNKNOWN` (`TraitHandler.cpp:33`), in-combat (`TraitHandler.cpp:37`), or in a running battleground
   (`TraitHandler.cpp:43`), diffs the entries, validates through `TraitMgr::ValidateConfig`, and either
   applies directly or starts the commit cast (`Player::UpdateTraitConfig`, `Entities/Player/Player.cpp:31134`,
   which casts `TraitMgr::COMMIT_COMBAT_TRAIT_CONFIG_CHANGES_SPELL_ID` when `withCastTime` is set and
   otherwise writes the update field in `finalizeTraitConfigUpdate`, `Player.cpp:31161`).
   Failure is `SMSG_TRAIT_CONFIG_COMMIT_FAILED` (`Server/Packets/TraitPackets.h:41`, opcode `Opcodes.cpp:2508`).
3. **Save a second build ("new config")**. `CMSG_CLASS_TALENTS_REQUEST_NEW_CONFIG` (`Opcodes.cpp:355`) →
   `HandleClassTalentsRequestNewConfig` (`TraitHandler.cpp:149`): only for `TraitConfigType::Combat`,
   capped at `TraitMgr::MAX_COMBAT_TRAIT_CONFIGS`, seeds the granted/free ranks
   (`TraitMgr::GetGrantedTraitEntriesForConfig`, `TraitHandler.cpp:184`), validates and creates it
   (`Player::CreateTraitConfig`, `Player.cpp:31061`). Rename/delete are
   `CMSG_CLASS_TALENTS_RENAME_CONFIG` (`Opcodes.cpp:354` → `TraitHandler.cpp:204`) and
   `CMSG_CLASS_TALENTS_DELETE_CONFIG` (`Opcodes.cpp:351` → `TraitHandler.cpp:209`); the starter build toggle
   is `CMSG_CLASS_TALENTS_SET_STARTER_BUILD_ACTIVE` (`Opcodes.cpp:356` → `TraitHandler.cpp:214`).
4. **Switch spec / activate a config**. `Player::ActivateTalentGroup` (`Player.cpp:30768`) installs the
   spec's active combat config and action bars.

## B. Legacy talent purchase (`Talent.dbc` ranks)

1. **Click a talent**. `CMSG_LEARN_TALENTS` (`Opcodes.cpp:650`) → `HandleLearnTalentsOpcode`
   (`Handlers/SkillHandler.cpp:30`): loops the requested ids, calls `Player::LearnTalent`
   (`Player.cpp:29279`) for each, and sends `SMSG_LEARN_TALENT_FAILED` for the ones that were refused
   (`Opcodes.cpp:1888`).
2. **Checks in `Player::LearnTalent`** (`Player.cpp:29279`): in combat / dead, no primary specialization,
   unknown talent, wrong spec or class, and `talentInfo->TierID >= MaxTalentTiers` (`Player.cpp:29302`).
   Note: at this commit there is **no talent-point counter** (`FreeTalentPoints` does not exist); the tree
   depth limit is the only budget check. `TalentPackets.h:69` still declares a `ClassicTalentInfoUpdate`
   with `UnspentTalentPoints`, but nothing on the server fills it.
3. **Replacing a talent in the same tier**. Any talent already bought in the new talent's tier is removed
   (`Player::LearnTalent` loop, `Player.cpp:29329`) after checking the player is resting or in a
   talent-changing area and the old talent is not on cooldown (`Player.cpp:29336`).
4. **Learn**. `Player::AddTalent` (`Player.cpp:2907`) → `Player::LearnSpell` (`Player.cpp:3515`); then
   `Player::SendTalentsInfoData` (`Player.cpp:29829`) pushes `SMSG_UPDATE_TALENT_DATA`
   (`Server/Packets/TalentPackets.h:75`, opcode `Opcodes.cpp:2548`).
5. **Reset at the trainer**. `CMSG_CONFIRM_RESPEC_WIPE` (`TalentPackets.h:106`) →
   `HandleConfirmRespecWipeOpcode` (`Handlers/SkillHandler.cpp:78`): `Creature::CanResetTalents`
   (`Entities/Creature/Creature.cpp:1365`), price from `Player::GetNextResetTalentsCost` (`Player.h:2076`),
   money check, then `Player::ResetTalents` (`Player.cpp:3859`), which removes every class talent, zeroes the
   purchased ranks of the active **trait config** while keeping granted ranks (`Player.cpp:3890`), saves, and
   charges through `IncreaseResetTalentsCostAndCounters` (`Player.cpp:3848`). The confirmation window itself
   is `Player::SendRespecWipeConfirm` (`Player.cpp:10108`, opcode `Opcodes.cpp:2340`).
6. **Pet talents** are the same code with `IsPetTalents` (`TalentPackets.h:124`) and are stored on the pet.

## Notes / limits

- Two storages: `PlayerTalentMap` per specialization (`GetTalentMap`, read in `SendTalentsInfoData`) and the
  trait config update fields. `ResetTalents` touches both — a change to one without the other leaves a
  bought rank visible in the tree.
- There is no talent-point counter: `Player::InitTalentForLevel` (`Player.cpp:2555`) instead raises the tier
  budget from `DB2Manager::GetNumTalentsAtLevel` (`Player.cpp:2567`), writes it into `MaxTalentTiers`
  (`Player.cpp:2604`) and strips ranks above it (`Player.cpp:2600`).
- Bots: the trait path needs a full `TraitConfig` snapshot (`TraitEntry` node ids + ranks), the legacy path
  needs only talent ids — both are reachable through the handlers above.
