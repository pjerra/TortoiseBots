#pragma once

#include "Define.h"

class Creature;
class Item;
class Player;

// Feeds the TortoiseBots dashboard (tools/observability) on ForeverCore in the dashboard's own datagram format, over UDP:
// HEARTBEAT + BOT_BATCH (the roster), SERVER_INFO, BOT_EVENTS and the BOT_DEATH / STUCK anomalies. Runs on the world thread.
namespace TortoiseBots::Dashboard
{
void LoadConfig();
void Update(uint32 diff);
// The item table the dashboard reads instead of world.item_template (ForeverCore keeps items in the DB2 stores).
void WriteItemsFile();

void OnKill(Player* killer, Creature* killed);
void OnKilledByCreature(Creature* killer, Player* killed);
void OnQuestStatusChange(Player* player, uint32 questId);
void OnLootItem(Player* player, Item* item);
void OnMoneyChanged(Player* player, int64 amount);
}
