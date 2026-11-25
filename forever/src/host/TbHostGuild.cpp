#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildPure.h"
#include "host/pool/TbPoolRegistry.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Creature.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "MailPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "PetitionMgr.h"
#include "PetitionPackets.h"
#include "Player.h"
#include "SocialMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "playerbot/policy/town/TownErrandPolicy.h"
#include "playerbot/policy/world/PoolRegistryPolicy.h"

namespace tbhost
{
namespace guild
{

namespace
{
bool SetFail(std::string* out, char const* reason)
{
    if (out)
        *out = reason;
    return false;
}
} // namespace

uint32_t RequiredPetitionSigns()
{
    return sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS);
}

bool BuyPetition(Player* bot, ObjectGuid const& petitionerNpcGuid, std::string const& guildName, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (bot->GetGuildId() != 0)
        return SetFail(failReason, "already-in-guild");
    if (guildName.empty())
        return SetFail(failReason, "empty-name");
    if (!bot->GetNPCIfCanInteractWith(petitionerNpcGuid, UNIT_NPC_FLAG_PETITIONER, UNIT_NPC_FLAG_2_NONE))
        return SetFail(failReason, "cannot-interact-petitioner");

    WorldPackets::Petition::PetitionBuy packet{ WorldPacket(CMSG_PETITION_BUY) };
    packet.Unit = petitionerNpcGuid;
    packet.Title = guildName;
    packet.Muid = 0;
    bot->GetSession()->HandlePetitionBuy(packet);

    if (!sPetitionMgr->GetPetitionByOwner(bot->GetGUID()))
        return SetFail(failReason, "core-refused");

    TC_LOG_INFO("tortoisebots", "[W9] petition-buy bot={} name={}", bot->GetName(), guildName);
    return true;
}

bool SignPetition(Player* signer, ObjectGuid const& petitionItemGuid, std::string* failReason)
{
    if (!signer || !signer->GetSession() || !signer->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (signer->GetGuildId() != 0)
        return SetFail(failReason, "already-in-guild");

    Petition const* before = sPetitionMgr->GetPetition(petitionItemGuid);
    if (!before)
        return SetFail(failReason, "petition-not-found");
    size_t const signsBefore = before->Signatures.size();

    WorldPackets::Petition::SignPetition packet{ WorldPacket(CMSG_SIGN_PETITION) };
    packet.PetitionGUID = petitionItemGuid;
    packet.Choice = 0;
    signer->GetSession()->HandleSignPetition(packet);

    Petition const* after = sPetitionMgr->GetPetition(petitionItemGuid);
    if (!after || after->Signatures.size() <= signsBefore)
        return SetFail(failReason, "core-refused");

    TC_LOG_INFO("tortoisebots", "[W9] petition-sign bot={} signs={}", signer->GetName(), uint32_t(after->Signatures.size()));
    return true;
}

bool TurnInPetition(Player* bot, ObjectGuid const& petitionItemGuid, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (bot->GetGuildId() != 0)
        return SetFail(failReason, "already-in-guild");
    if (!bot->GetItemByGuid(petitionItemGuid))
        return SetFail(failReason, "charter-not-in-bags");

    WorldPackets::Petition::TurnInPetition packet{ WorldPacket(CMSG_TURN_IN_PETITION) };
    packet.Item = petitionItemGuid;
    bot->GetSession()->HandleTurnInPetition(packet);

    if (bot->GetGuildId() == 0)
        return SetFail(failReason, "core-refused");

    TC_LOG_INFO("tortoisebots", "[W9] guild-created bot={} guild={}", bot->GetName(), bot->GetGuildId());
    return true;
}

uint64_t GetGuildId(Player const* player)
{
    return player ? player->GetGuildId() : 0;
}

ObjectGuid GetLeaderGuid(uint64_t guildId)
{
    Guild const* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    return guild ? guild->GetLeaderGUID() : ObjectGuid::Empty;
}

bool GetMemberRankId(uint64_t guildId, ObjectGuid const& memberGuid, uint8_t& outRankId)
{
    Guild const* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    if (!guild)
        return false;
    Guild::Member const* member = guild->GetMember(memberGuid);
    if (!member)
        return false;
    outRankId = static_cast<uint8_t>(member->GetRankId());
    return true;
}

bool HasAnyRankRight(uint64_t guildId, ObjectGuid const& memberGuid, uint32_t rights)
{
    Guild const* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    if (!guild)
        return false;
    Guild::Member const* member = guild->GetMember(memberGuid);
    if (!member)
        return false;
    return guild->HasAnyRankRight(member->GetRankId(), static_cast<GuildRankRights>(rights));
}

bool CanEditOfficerNotes(uint64_t guildId, ObjectGuid const& memberGuid)
{
    return HasAnyRankRight(guildId, memberGuid, GR_RIGHT_EOFFNOTE);
}

bool IsMemberOnline(uint64_t guildId, ObjectGuid const& memberGuid)
{
    Guild const* guild = guildId ? sGuildMgr->GetGuildById(guildId) : nullptr;
    if (!guild)
        return false;
    Guild::Member const* member = guild->GetMember(memberGuid);
    return member && member->IsOnline();
}

// The account test of host/TbHostThievery.cpp without its manager shortcut: the leader's ACCOUNT is the pool's, so an
// owned alt leading a player's guild is not a pool character and its guild is not bot-run (TBF #30).
bool IsPoolCharacter(ObjectGuid const& guid)
{
    if (guid.IsEmpty())
        return false;
    uint32_t const accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (!accountId)
        return false;
    if (TortoiseBots::pool::AccountRegistered(accountId))
        return true;
    std::string accountName;
    return AccountMgr::GetName(accountId, accountName) && TortoiseBots::world::IsPoolAccountName(accountName);
}

InviteOutcome InviteByName(Player* inviter, std::string const& targetName, std::string* failReason)
{
    auto const fail = [failReason](InviteOutcome outcome)
    {
        if (failReason)
            *failReason = InviteOutcomeCode(outcome);
        return outcome;
    };

    if (!inviter || !inviter->GetSession() || !inviter->IsInWorld())
        return fail(InviteOutcome::NotInWorld);
    if (inviter->GetGuildId() == 0)
        return fail(InviteOutcome::NotInGuild);
    std::string name = targetName;
    if (!normalizePlayerName(name))
        return fail(InviteOutcome::EmptyTarget);

    // Guild::HandleInviteMember answers the inviter for each of these and sends nothing at all for an ignore, so the
    // seam checks the same conditions in the same order and returns which one would have fired.
    Player* invitee = ObjectAccessor::FindPlayerByName(name);
    if (!invitee)
        return fail(InviteOutcome::TargetNotFound);
    if (invitee->GetSocial()->HasIgnore(inviter->GetGUID(), inviter->GetSession()->GetAccountGUID()))
        return fail(InviteOutcome::TargetIgnoring);
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && invitee->GetTeam() != inviter->GetTeam())
        return fail(InviteOutcome::WrongFaction);
    if (invitee->GetGuildId())
        return fail(InviteOutcome::TargetInGuild);
    if (invitee->GetGuildIdInvited())
        return fail(InviteOutcome::TargetAlreadyInvited);
    if (!HasAnyRankRight(inviter->GetGuildId(), inviter->GetGUID(), GR_RIGHT_INVITE))
        return fail(InviteOutcome::NoInviteRight);

    WorldPackets::Guild::GuildInviteByName packet{ WorldPacket(CMSG_GUILD_INVITE_BY_NAME) };
    packet.Name = name;
    inviter->GetSession()->HandleGuildInviteByName(packet);

    if (invitee->GetGuildIdInvited() != inviter->GetGuildId())
        return fail(InviteOutcome::CoreRefused);
    return InviteOutcome::Ok;
}

bool AcceptInvite(Player* invitee, uint64_t guildId, std::string* failReason)
{
    if (!invitee || !invitee->GetSession() || !invitee->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (invitee->GetGuildId() != 0)
        return SetFail(failReason, "already-in-guild");
    if (!guildId)
        return SetFail(failReason, "no-guild-id");

    WorldPackets::Guild::AcceptGuildInvite packet{ WorldPacket(CMSG_ACCEPT_GUILD_INVITE) };
    packet.GuildGuid = ObjectGuid::Create<HighGuid::Guild>(guildId);
    invitee->GetSession()->HandleGuildAcceptInvite(packet);

    if (invitee->GetGuildId() != guildId)
        return SetFail(failReason, "core-refused");

    guild_order_access::ApplyBotRankOnJoin(invitee, static_cast<uint32_t>(guildId));
    TC_LOG_INFO("tortoisebots", "[W9] guild-join bot={} guild={}", invitee->GetName(), guildId);
    return true;
}

bool DeclineInvite(Player* invitee, std::string* failReason)
{
    if (!invitee || !invitee->GetSession() || !invitee->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (invitee->GetGuildId() != 0)
        return SetFail(failReason, "already-in-guild");
    if (!invitee->GetGuildIdInvited())
        return SetFail(failReason, "no-invite");

    WorldPackets::Guild::GuildDeclineInvitation packet{ WorldPacket(CMSG_GUILD_DECLINE_INVITATION) };
    invitee->GetSession()->HandleGuildDeclineInvitation(packet);

    if (invitee->GetGuildIdInvited() != 0)
        return SetFail(failReason, "core-refused");
    return true;
}

bool SetMemberRank(Player* setter, ObjectGuid const& targetGuid, uint8_t rankOrder, std::string* failReason)
{
    if (!setter || !setter->GetSession() || !setter->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (setter->GetGuildId() == 0)
        return SetFail(failReason, "not-in-guild");

    WorldPackets::Guild::GuildAssignMemberRank packet{ WorldPacket(CMSG_GUILD_ASSIGN_MEMBER_RANK) };
    packet.Member = targetGuid;
    packet.RankOrder = static_cast<int32>(rankOrder);
    setter->GetSession()->HandleGuildAssignRank(packet);
    return true;
}

bool LeaveGuild(Player* bot, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = bot->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");

    WorldPackets::Guild::GuildLeave packet{ WorldPacket(CMSG_GUILD_LEAVE) };
    bot->GetSession()->HandleGuildLeave(packet);

    if (bot->GetGuildId() == guildId)
        return SetFail(failReason, "core-refused");

    TC_LOG_INFO("tortoisebots", "[W9] guild-leave bot={} guild={}", bot->GetName(), guildId);
    return true;
}

bool PromoteMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason)
{
    if (!actor || !actor->GetSession() || !actor->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = actor->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || !guild->GetMember(targetGuid))
        return SetFail(failReason, "not-a-member");
    if (!HasAnyRankRight(guildId, actor->GetGUID(), GR_RIGHT_PROMOTE))
        return SetFail(failReason, "no-rank-right");

    WorldPackets::Guild::GuildPromoteMember packet{ WorldPacket(CMSG_GUILD_PROMOTE_MEMBER) };
    packet.Promotee = targetGuid;
    actor->GetSession()->HandleGuildPromoteMember(packet);
    return true;
}

bool DemoteMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason)
{
    if (!actor || !actor->GetSession() || !actor->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = actor->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || !guild->GetMember(targetGuid))
        return SetFail(failReason, "not-a-member");
    if (!HasAnyRankRight(guildId, actor->GetGUID(), GR_RIGHT_DEMOTE))
        return SetFail(failReason, "no-rank-right");

    WorldPackets::Guild::GuildDemoteMember packet{ WorldPacket(CMSG_GUILD_DEMOTE_MEMBER) };
    packet.Demotee = targetGuid;
    actor->GetSession()->HandleGuildDemoteMember(packet);
    return true;
}

bool RemoveMember(Player* actor, ObjectGuid const& targetGuid, std::string* failReason)
{
    if (!actor || !actor->GetSession() || !actor->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = actor->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || !guild->GetMember(targetGuid))
        return SetFail(failReason, "not-a-member");
    if (!HasAnyRankRight(guildId, actor->GetGUID(), GR_RIGHT_REMOVE))
        return SetFail(failReason, "no-rank-right");

    WorldPackets::Guild::GuildOfficerRemoveMember packet{ WorldPacket(CMSG_GUILD_OFFICER_REMOVE_MEMBER) };
    packet.Removee = targetGuid;
    actor->GetSession()->HandleGuildOfficerRemoveMember(packet);
    return true;
}

bool SetLeader(Player* actor, std::string const& targetName, std::string* failReason)
{
    if (!actor || !actor->GetSession() || !actor->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = actor->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");
    std::string name = targetName;
    if (!normalizePlayerName(name))
        return SetFail(failReason, "empty-target");
    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild || guild->GetLeaderGUID() != actor->GetGUID())
        return SetFail(failReason, "not-leader");
    bool member = false;
    for (auto const& [guid, slot] : guild->GetMembers())
        if (slot.GetName() == name)
        {
            member = true;
            break;
        }
    if (!member)
        return SetFail(failReason, "not-a-member");

    WorldPackets::Guild::GuildSetGuildMaster packet{ WorldPacket(CMSG_GUILD_SET_GUILD_MASTER) };
    packet.NewMasterName = name;
    actor->GetSession()->HandleGuildSetGuildMaster(packet);
    return true;
}

uint32_t CharterCost()
{
    return uint32_t(sWorld->getIntConfig(CONFIG_CHARTER_COST_GUILD));
}

bool SayToGuild(Player* bot, std::string const& text, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    uint64_t const guildId = bot->GetGuildId();
    if (!guildId)
        return SetFail(failReason, "not-in-guild");
    if (text.empty())
        return SetFail(failReason, "empty-text");

    Guild const* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return SetFail(failReason, "no-such-guild");
    Guild::Member const* member = guild->GetMember(bot->GetGUID());
    if (!member)
        return SetFail(failReason, "not-a-member");
    if (!guild->HasAnyRankRight(member->GetRankId(), GR_RIGHT_GCHATSPEAK))
        return SetFail(failReason, "no-speak-right");

    guild->BroadcastToGuild(bot->GetSession(), false, text, LANG_UNIVERSAL);
    return true;
}

bool DepositMoney(Player* bot, ObjectGuid const& bankerGuid, uint64_t copper, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (bot->GetGuildId() == 0)
        return SetFail(failReason, "not-in-guild");
    if (!copper)
        return SetFail(failReason, "zero-amount");
    if (!bot->HasEnoughMoney(copper))
        return SetFail(failReason, "not-enough-money");
    if (!bot->GetGameObjectIfCanInteractWith(bankerGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return SetFail(failReason, "cannot-interact-bank");

    uint64_t const before = bot->GetMoney();
    WorldPackets::Guild::GuildBankDepositMoney packet{ WorldPacket(CMSG_GUILD_BANK_DEPOSIT_MONEY) };
    packet.Banker = bankerGuid;
    packet.Money = copper;
    bot->GetSession()->HandleGuildBankDepositMoney(packet);

    if (bot->GetMoney() >= before)
        return SetFail(failReason, "core-refused");

    TC_LOG_INFO("tortoisebots", "[W9] bank-deposit-money bot={} guild={} copper={}", bot->GetName(), bot->GetGuildId(), before - bot->GetMoney());
    return true;
}

bool DepositItem(Player* bot, ObjectGuid const& bankerGuid, uint8_t tabId, Item* item, std::string* failReason)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (bot->GetGuildId() == 0)
        return SetFail(failReason, "not-in-guild");
    if (!item)
        return SetFail(failReason, "null-item");
    if (!bot->GetGameObjectIfCanInteractWith(bankerGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return SetFail(failReason, "cannot-interact-bank");

    static_assert(kGuildBankAutoSlot == NULL_SLOT, "kGuildBankAutoSlot must match core NULL_SLOT");

    uint32_t const itemId = item->GetEntry();
    uint32_t const beforeCount = bot->GetItemCount(itemId, false);
    uint8_t const bagSlot = item->GetBagSlot();
    uint8_t const itemSlot = item->GetSlot();

    WorldPackets::Guild::AutoGuildBankItem packet{ WorldPacket(CMSG_AUTO_GUILD_BANK_ITEM) };
    packet.Banker = bankerGuid;
    packet.BankTab = tabId;
    packet.BankSlot = kGuildBankAutoSlot;
    packet.ContainerSlot = bagSlot;
    packet.ContainerItemSlot = itemSlot;
    bot->GetSession()->HandleAutoGuildBankItem(packet);

    uint32_t const afterCount = bot->GetItemCount(itemId, false);
    Item* slotAfter = bot->GetItemByPos(bagSlot, itemSlot);
    uint32_t const slotAfterEntry = slotAfter ? slotAfter->GetEntry() : 0;
    if (!VerifyBankDeposit(beforeCount, afterCount, itemId, slotAfterEntry))
        return SetFail(failReason, "bank-refused");

    TC_LOG_INFO("tortoisebots", "[W9] bank-deposit-item bot={} guild={} tab={} item={} count={}",
        bot->GetName(), bot->GetGuildId(), uint32_t(tabId), itemId, beforeCount - afterCount);
    return true;
}

bool SendMail(Player* sender, ObjectGuid const& mailboxGuid, std::string const& targetName,
    std::string const& subject, std::string const& body, uint64_t copper,
    std::vector<ObjectGuid> const& attachmentGuids, std::string* failReason)
{
    if (!sender || !sender->GetSession() || !sender->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (targetName.empty())
        return SetFail(failReason, "empty-target");
    if (attachmentGuids.size() > MAX_CLIENT_MAIL_ITEMS)
        return SetFail(failReason, "too-many-attachments");

    bool const canInteract = mailboxGuid.IsGameObject()
        ? sender->GetGameObjectIfCanInteractWith(mailboxGuid, GAMEOBJECT_TYPE_MAILBOX) != nullptr
        : (mailboxGuid.IsAnyTypeCreature() && sender->GetNPCIfCanInteractWith(mailboxGuid, UNIT_NPC_FLAG_MAILBOX, UNIT_NPC_FLAG_2_NONE) != nullptr);
    if (!canInteract)
        return SetFail(failReason, "cannot-interact-mailbox");

    uint32_t const postage = attachmentGuids.empty() ? 30u : uint32_t(30u * attachmentGuids.size());
    if (!sender->HasEnoughMoney(copper + postage))
        return SetFail(failReason, "not-enough-money");

    uint64_t const beforeMoney = sender->GetMoney();
    uint32_t trackEntry = 0;
    uint32_t beforeItemCount = 0;
    if (!attachmentGuids.empty())
    {
        if (Item* firstAtt = sender->GetItemByGuid(attachmentGuids[0]))
        {
            trackEntry = firstAtt->GetEntry();
            beforeItemCount = trackEntry ? sender->GetItemCount(trackEntry, false) : 0;
        }
    }

    WorldPackets::Mail::SendMail packet{ WorldPacket(CMSG_SEND_MAIL) };
    packet.Info.Mailbox = mailboxGuid;
    packet.Info.StationeryID = MAIL_STATIONERY_DEFAULT;
    packet.Info.SendMoney = static_cast<int64>(copper);
    packet.Info.Cod = 0;
    packet.Info.Target = targetName;
    packet.Info.Subject = subject;
    packet.Info.Body = body;
    for (size_t i = 0; i < attachmentGuids.size(); ++i)
    {
        WorldPackets::Mail::SendMail::StructSendMail::MailAttachment att;
        att.AttachPosition = static_cast<uint8>(i);
        att.ItemGUID = attachmentGuids[i];
        packet.Info.Attachments.push_back(att);
    }

    sender->GetSession()->HandleSendMail(packet);
    uint32_t const afterItemCount = trackEntry ? sender->GetItemCount(trackEntry, false) : 0;
    if (!ai::MailSendConfirmed(trackEntry, beforeItemCount, afterItemCount, copper + postage, beforeMoney, sender->GetMoney()))
        return SetFail(failReason, "mail-refused");

    TC_LOG_INFO("tortoisebots", "[W9] mail-send bot={} to={} items={} copper={}",
        sender->GetName(), targetName, uint32_t(attachmentGuids.size()), copper);
    return true;
}

} // namespace guild
} // namespace tbhost
