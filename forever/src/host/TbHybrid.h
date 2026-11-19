// Slice 10, host package H10: the state keys of the druid, paladin and shaman (FOREVER-SLICE10-HYBRIDS-SPEC section 7; forever/docs/SLICE10-H-NOTES.md).
//
// Appended to every bot record of `.tb state` (after `practice=`; the older keys are unchanged). Read only, core API only (no engine call): the record is
// the same in a bot with or without a running engine. Compiled with and without TORTOISEBOTS_SLICE1.
//   form=<id|none>      the shapeshift form id the bot is in now (Player::GetShapeshiftForm: bear 5, cat 1, ghost wolf 16), `none` outside a form.
//   totems=<entry>:<sec>;...  the bot's live totems from its four totem summon slots (Unit::m_SummonSlot[1..4], read only): creature entry and the seconds
//                       left of the totem's duration (Totem::GetTotemDuration); empty when it has none.
//   imbue=<enchant id>:<sec>  the temporary enchantment of the main-hand weapon (a shaman's imbue, a rogue's poison): enchantment id and seconds left; empty when none.
//   seal=<aura id>      a paladin's own active seal (an aura of its spell family named "Seal of ..."); empty otherwise.
//   paura=<aura id>     a paladin's own active aura (the family's "... Aura" spells: Devotion, Retribution, Concentration, ...); empty otherwise.
// `power=` is NOT added: the record has carried `power=<mana|rage|energy>` with `pw=`, `pwmax=` (and `manapct=`) since slice 3, and a key appears once per line.
// Threading: world thread (TbH3.h).
#pragma once

class Player;

namespace TortoiseBots
{
namespace h3 { class Rec; class Reply; }
namespace h10
{
// The five keys above, appended to `rec`.
void AppendHybridState(h3::Rec& rec, Player* bot);
// `.tb grammar selftest`: one example of every record this package adds.
void SelfTestRecords(h3::Reply& reply);
}
}
