// The `blessing` addon verb (TBF #133 piece c-blessing): per-class and per-player
// paladin blessing orders over the TBM addon channel (`TbAddonChannel.h`), plus
// `.tb blessing` for GM testing. Pure parsing and formatting live in `TbAddonBlessingPure.h`.
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace blessing
{
void RegisterAddonVerbsBlessing();
bool HandleBlessingChat(ChatHandler* handler, Trinity::ChatCommands::Tail args);
} // namespace blessing
} // namespace TortoiseBots
