#include "host/TbHostGuild.h"

#include "Creature.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "MailPackets.h"
#include "Opcodes.h"
#include "PetitionMgr.h"
#include "PetitionPackets.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

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

bool InviteByName(Player* inviter, std::string const& targetName, std::string* failReason)
{
    if (!inviter || !inviter->GetSession() || !inviter->IsInWorld())
        return SetFail(failReason, "not-in-world");
    if (inviter->GetGuildId() == 0)
        return SetFail(failReason, "not-in-guild");
    if (targetName.empty())
        return SetFail(failReason, "empty-target");

    WorldPackets::Guild::GuildInviteByName packet{ WorldPacket(CMSG_GUILD_INVITE_BY_NAME) };
    packet.Name = targetName;
    inviter->GetSession()->HandleGuildInviteByName(packet);
    return true;
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

    TC_LOG_INFO("tortoisebots", "[W9] guild-join bot={} guild={}", invitee->GetName(), guildId);
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

    uint32_t const itemId = item->GetEntry();
    uint32_t const beforeCount = bot->GetItemCount(itemId, false);

    WorldPackets::Guild::AutoGuildBankItem packet{ WorldPacket(CMSG_AUTO_GUILD_BANK_ITEM) };
    packet.Banker = bankerGuid;
    packet.BankTab = tabId;
    packet.BankSlot = 0;
    packet.ContainerSlot = item->GetBagSlot();
    packet.ContainerItemSlot = item->GetSlot();
    bot->GetSession()->HandleAutoGuildBankItem(packet);

    uint32_t const afterCount = bot->GetItemCount(itemId, false);
    if (afterCount >= beforeCount)
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
    TC_LOG_INFO("tortoisebots", "[W9] mail-send bot={} to={} items={} copper={}",
        sender->GetName(), targetName, uint32_t(attachmentGuids.size()), copper);
    return true;
}

} // namespace guild
} // namespace tbhost
