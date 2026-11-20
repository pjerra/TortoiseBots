# Trainers — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root.

## Client side, step by step

1. **Talk to the NPC**. `CMSG_TALK_TO_GOSSIP` (`Opcodes.cpp:1116`) → `HandleGossipHelloOpcode`
   (`Handlers/NPCHandler.cpp:161`) → `Player::PrepareGossipMenu` (`Entities/Player/Player.cpp:15025`).
   A trainer creature gets the extra "Train me." option when its class mask matches
   (`Player.cpp:15173`); the class gate itself is evaluated at `Player.cpp:15082`.
2. **Pick "Train me."** → `HandleGossipSelectOptionOpcode` (`NPCHandler.cpp:199`) →
   `Player::OnGossipSelect` (`Player.cpp:15217`), case `GossipOptionNpc::Trainer` at `Player.cpp:15315`,
   which resolves the trainer id (gossip option first, creature trainer as fallback) and calls
   `WorldSession::SendTrainerList` (`NPCHandler.cpp:112`).
   A client that opens the window directly uses `CMSG_TRAINER_LIST` (`Opcodes.cpp:1131`) →
   `HandleTrainerListOpcode` (`NPCHandler.cpp:97`), same body.
3. **The trainer window**. `SendTrainerList` records the interaction
   (`PlayerInteractionType::Trainer`) and calls `Trainer::SendSpells` (`Entities/Creature/Trainer.cpp:117`):
   every spell of the trainer is filtered by class/race (`Trainer.cpp:131`) and trainer conditions (`Trainer.cpp:136`), its state is
   computed by `Trainer::GetSpellState` (`Trainer.cpp:394` — known / available / unavailable: class mask,
   `IsSpellFitByClassAndRace`, `ReqSkillLine`/`ReqSkillRank`, `ReqAbility`, `ReqLevel`, learn-spell rank
   checks) and written into the `Usable` field (`Trainer.cpp:156`) of `SMSG_TRAINER_LIST`
   (`Server/Packets/NPCPackets.h:202`, opcode `Opcodes.cpp:2507`).
4. **Buy a spell**. `CMSG_TRAINER_BUY_SPELL` (`Opcodes.cpp:1130`) → `HandleTrainerBuySpellOpcode`
   (`NPCHandler.cpp:130`): requires the NPC (trainer flag, or gossip flag when the window was opened from
   the gossip option, `NPCHandler.cpp:136`), an active `PlayerInteractionType::Trainer` interaction and the
   same trainer id as the open window, then `Trainer::TeachSpell` (`Trainer.cpp:165`).
5. **Cost and prerequisite checks**. `Trainer::CanTeachSpell` (`Trainer.cpp:371`) rejects anything not in
   `SpellState::Available` and the second primary-profession first rank without a free profession slot;
   `TeachSpell` then applies the reputation discount (`GetReputationPriceDiscount`, `Trainer.cpp:191`),
   refuses on short money (`Trainer.cpp:192`) and charges with `ModifyMoney` (`Trainer.cpp:198`).
6. **Learn**. Castable trainer spells are cast on the player (`Trainer.cpp:207`), otherwise
   `Player::LearnSpell` (`Player.cpp:3515`) adds the spell and sends `SMSG_LEARNED_SPELLS`
   (`Player.cpp:3528`, opcode `Opcodes.cpp:1886`) — that packet *is* the buy confirmation; there is no
   separate success opcode at this commit. Pet trainers re-send the list afterwards (`Trainer.cpp:211`).
   Failures use `Trainer::SendTeachFailure` (`Trainer.cpp:463`) → `SMSG_TRAINER_BUY_FAILED`
   (`Opcodes.cpp:2506`).
7. **Bulk training** (used by the bot code): `Trainer::SendSpells` is exposed so a caller can compute the
   same "available" set without going through the window (`Trainer.h:75` comment); the authoritative gate
   is still `TeachSpell`, so a bot must call it per spell.

## Notes / limits

- Trainer contents come from `trainer` + `trainer_spell` (creature_template.trainerId →
  `sObjectMgr->GetTrainer`), including `MoneyCost`, `ReqLevel`, `ReqSkillLine`, `ReqSkillRank`,
  `ReqAbility1..3`.
- `CanTeachSpell` does **not** re-check level/skill/ability; it relies on `GetSpellState` — so any change
  to prerequisites only needs to land in `GetSpellState` (`Trainer.cpp:394`).
- There is no trainer gossip menu generated from the spell list: the only entry point is the auto
  "Train me." option (`Player.cpp:15173`) or the client's own `CMSG_TRAINER_LIST`.
