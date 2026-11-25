#pragma once

#include "ObjectGuid.h"
#include "host/TbHostGuildInvitePure.h"

#include <cstdint>
#include <string>
#include <vector>

class Item;
class Player;

namespace tbhost
{
namespace guild
{

// Founding (blizzlike petition flow).
uint32_t RequiredPetitionSigns();
bool BuyPetition(Player* bot, ObjectGuid const& petitionerNpcGuid, std::string const& guildName, std::string* failReason = nullptr);
bool SignPetition(Player* signer, ObjectGuid const& petitionItemGuid, std::string* failReason = nullptr);
bool TurnInPetition(Player* bot, ObjectGuid const& petitionItemGuid, std::string* failReason = nullptr);

// Guild queries and rank rights.
uint64_t GetGuildId(Player const* player);
ObjectGuid GetLeaderGuid(uint64_t guildId);
bool GetMemberRankId(uint64_t guildId, ObjectGuid const& memberGuid, uint8_t& outRankId);
bool HasAnyRankRight(uint64_t guildId, ObjectGuid const& memberGuid, uint32_t rights);
bool CanEditOfficerNotes(uint64_t guildId, ObjectGuid const& memberGuid);
bool IsMemberOnline(uint64_t guildId, ObjectGuid const& memberGuid);
// A pool (bot) character by account: the guild leader test that tells a bot-run guild from a player's own (TBF #30).
bool IsPoolCharacter(ObjectGuid const& guid);

// Membership and ranks.
InviteOutcome InviteByName(Player* inviter, std::string const& targetName, std::string* failReason = nullptr);
bool AcceptInvite(Player* invitee, uint64_t guildId, std::string* failReason = nullptr);
bool DeclineInvite(Player* invitee, std::string* failReason = nullptr);
bool SetMemberRank(Player* setter, ObjectGuid const& targetGuid, uint8_t rankOrder, std::string* failReason = nullptr);

// Guild management through the core's own handlers (the owner's `guild <verb>` whispers).
bool LeaveGuild(Player* bot, std::string* failReason = nullptr);
bool PromoteMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason = nullptr);
bool DemoteMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason = nullptr);
bool RemoveMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason = nullptr);
bool SetLeader(Player* actor, std::string const& targetName, std::string* failReason = nullptr);

// The core's charter price (CONFIG_CHARTER_COST_GUILD) and a line in the bot's guild chat (its rank must allow speech).
uint32_t CharterCost();
bool SayToGuild(Player* bot, std::string const& text, std::string* failReason = nullptr);

// Guild bank DEPOSIT only (contents are private in ForeverCore).
bool DepositMoney(Player* bot, ObjectGuid const& bankerGuid, uint64_t copper, std::string* failReason = nullptr);
bool DepositItem(Player* bot, ObjectGuid const& bankerGuid, uint8_t tabId, Item* item, std::string* failReason = nullptr);

// Mail SEND (through HandleSendMail; postage and delivery delay are the core's).
bool SendMail(Player* sender, ObjectGuid const& mailboxGuid, std::string const& targetName,
    std::string const& subject, std::string const& body, uint64_t copper,
    std::vector<ObjectGuid> const& attachmentGuids, std::string* failReason = nullptr);

} // namespace guild
} // namespace tbhost
