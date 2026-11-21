#pragma once
// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:unknown_type_name,clang:use_of_undeclared_identifier
#include "ObjectGuid.h"
#include "BotChatGate.h"
#include <map>
#include <unordered_map>
#include <mutex>

class Player;
class PlayerbotAI;

// Module-local PlayerbotAI registry. The Tortoise core remains unaware of
// PlayerBots; the module maps live Player*/ObjectGuid identities to AI objects
// through the Headless GUID lifecycle owned by BotManager.

class PlayerbotAIStorage
{
public:
    static PlayerbotAIStorage& Instance();

    void SetAI(Player* player, PlayerbotAI* ai);
    void RemoveAI(Player* player);
    PlayerbotAI* GetAI(Player* player) const;
    PlayerbotAI* GetAI(ObjectGuid guid) const;
    // Any thread (issue #844): the guid of a player that has a registered AI,
    // found by pointer comparison only, so a packet hook can key deferred
    // work without dereferencing the Player or touching the AI.
    // With `gate`, also the chat gate the AI published (issue #846; a default
    // gate, which filters nothing, until it has published one).
    bool GetBotGuid(Player const* player, ObjectGuid& guid, TortoiseBots::BotChatGate* gate = nullptr) const;
    // World thread: the AI's update publishes its chat gate when it changes.
    // Ignored for a player without a registered AI.
    void SetBotChatGate(Player const* player, TortoiseBots::BotChatGate const& gate);
    void SetPlayerForcedRole(ObjectGuid guid, uint8 role);
    uint8 GetPlayerForcedRole(ObjectGuid guid) const;
    void ClearPlayerForcedRole(ObjectGuid guid);
    void SetPlayerForcedSpecTab(ObjectGuid guid, int8 tab);
    int8 GetPlayerForcedSpecTab(ObjectGuid guid) const;
    void ClearPlayerForcedSpecTab(ObjectGuid guid);

private:
    mutable std::mutex mutex_;
    std::unordered_map<ObjectGuid, PlayerbotAI*> byGuid_;
    std::unordered_map<Player*, PlayerbotAI*> byPlayer_;
    std::unordered_map<Player const*, ObjectGuid> guidByPlayer_;
    std::unordered_map<Player const*, TortoiseBots::BotChatGate> chatGateByPlayer_;
    // Human role overrides stay separate from the PlayerbotAI registries.
    std::unordered_map<ObjectGuid, uint8> playerForcedRoles_;
    std::unordered_map<ObjectGuid, int8> playerForcedSpecTabs_;
};

#ifndef GET_PLAYERBOT_AI
#define GET_PLAYERBOT_AI(player) PlayerbotAIStorage::Instance().GetAI(player)
#endif
