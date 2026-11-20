#pragma once
#include "ObjectGuid.h"
#include <string>
class Player;
class PlayerbotAI;
namespace TortoiseBots::onyxia
{
void InstallGround(PlayerbotAI* ai);
std::string ConfigureGround(Player* bot, ObjectGuid boss, Player* tank);
void ForgetGround(ObjectGuid bot);
void GroundTick(Player* bot, PlayerbotAI* ai);
ObjectGuid GroundBoss(ObjectGuid bot);
ObjectGuid GroundOwner(ObjectGuid bot);
}
