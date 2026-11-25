// Additions to the hire contract (TbHire.h) that the module's startup wiring calls. The contract's own
// functions stay as they are; this header only carries the add-on capability advertisement, which needs
// the engine config loaded (TbScripts.cpp, OnWorldInitialized) and therefore cannot run at script
// registration time. Built only with TORTOISEBOTS_SLICE1, like TbHire.cpp.
#pragma once

#include <string>
#include <vector>

namespace TortoiseBots
{
namespace hire
{

// Advertises `hiring` when hiring is enabled and `hire-anywhere` only when the fast path is allowed.
void AdvertiseAddonCaps();

// AiPlayerbot.ShamanTankEnabled, for the `.bot hire` parser without the engine config header.
bool ShamanTankEnabled();

// The TBM:FEATURE rows of the hire switches (hiring, hire-max, hire-pvp, hire-anywhere, hire-tier), from the live
// engine config. TbAddonInfo.cpp appends them to `.bot features` / the add-on Info window.
std::vector<std::string> FeatureLines();

} // namespace hire
} // namespace TortoiseBots
