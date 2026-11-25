// World buffs at the six capital recruiters (TbHire.h package P5). Built with the engine only: the gossip page, the
// purchase and the unlock hooks live in TbHireWorldBuffs.cpp behind TORTOISEBOTS_SLICE1, like the rest of the hire port.
#pragma once

namespace TortoiseBots::hire::worldbuffs
{
// Registers the aura-credit script (spell_script_names), the kill/PvP credit hooks and the raid-entry rule.
void AddScripts();
} // namespace TortoiseBots::hire::worldbuffs
