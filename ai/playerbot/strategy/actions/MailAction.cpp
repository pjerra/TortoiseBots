
#include "Mail/Mail.h"
#include "playerbot/playerbot.h"
#include "MailAction.h"
#include "MapNodes/MasterPlayer.h"
#include "playerbot/TbCompatW2Town.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/Helpers.h"
#include "runtime/GuildOrderRelistPolicy.h"
#include "runtime/GuildOrderStore.h"
#include "host/ModuleLog.h"

#include <algorithm>
#include <cstdlib>

using namespace ai;

namespace
{
MasterPlayer* GetMailOwner(Player* bot)
{
    return MailOwnerCompat(bot);
}

// The core's auction notification subject is "<item>:<random>:<state>"
// (Mail.h AuctionMailState). Anything else is not an auction notification.
bool ParseAuctionSubject(std::string const& subject, uint32_t& itemId, uint32_t& state)
{
    std::vector<std::string> parts = split(subject, ':');
    if (parts.size() != 3 || parts[0].empty() || parts[2].empty())
        return false;
    char* end = nullptr;
    unsigned long const item = std::strtoul(parts[0].c_str(), &end, 10);
    if (!end || *end)
        return false;
    unsigned long const sentinel = std::strtoul(parts[2].c_str(), &end, 10);
    if (!end || *end)
        return false;
    itemId = uint32_t(item);
    state = uint32_t(sentinel);
    return true;
}

// A sale notification body is "<bidder>:<bid>:<buyout>:<deposit>:<cut>"
// (AuctionHouseMgr::SendAuctionSuccessfulMail); these are the listing terms
// the ledger row was posted with, so they identify the auction that settled.
void ParseAuctionSaleBody(std::string const& text, TortoiseBots::GuildOrders::RelistMail& view)
{
    std::vector<std::string> parts = split(text, ':');
    if (parts.size() != 5)
        return;
    uint32_t values[3] = {};
    for (size_t i = 0; i < 3; ++i)
    {
        if (parts[i + 1].empty())
            return;
        char* end = nullptr;
        unsigned long const value = std::strtoul(parts[i + 1].c_str(), &end, 10);
        if (!end || *end)
            return;
        values[i] = uint32_t(value);
    }
    view.bodyParsed = true;
    view.bodyBid = values[0];
    view.bodyBuyout = values[1];
    view.bodyDeposit = values[2];
}

TortoiseBots::GuildOrders::RelistMail MakeRelistMailView(Mail* mail, MasterPlayer* owner)
{
    TortoiseBots::GuildOrders::RelistMail view;
    view.auction = MailIsAuctionCompat(mail);
    view.hasItems = MailHasItemsCompat(mail);
    view.money = mail->money;
    ParseAuctionSubject(mail->subject, view.subjectItemId, view.subjectState);
    if (view.auction && view.subjectState == TortoiseBots::GuildOrders::kRelistMailSuccessful &&
        MailHasTextCompat(mail))
        ParseAuctionSaleBody(MailBodyTextCompat(mail), view);
    if (view.hasItems)
    {
        Item* item = MailItemCompat(owner, mail->items.front().item_guid);
        if (item)
        {
            view.itemId = item->GetEntry();
            view.count = item->GetCount();
        }
    }
    return view;
}
}

std::map<std::string, MailProcessor*> MailAction::processors;

class TellMailProcessor : public MailProcessor
{
public:
    bool Before(Player* requester, PlayerbotAI* ai) override
    {
        ai->TellPlayer(requester, "=== Mailbox ===", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        tells.clear();
        return true;
    }

    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        Player* bot = ai->GetBot();
        MasterPlayer* mailOwner = GetMailOwner(bot);
        if (!mailOwner)
            return false;

        time_t cur_time = time(0);
        int days = (cur_time - mail->deliver_time) / 3600 / 24;
        std::ostringstream out;
        out << "#" << (index+1) << " ";
        if (!mail->money && !MailHasItemsCompat(mail))
            out << "|cffffffff" << mail->subject;

        if (mail->money)
        {
            out << "|cffffff00" << ChatHelper::formatMoney(mail->money);
            if (!mail->subject.empty()) out << " |cffa0a0a0(" << mail->subject << ")";
        }

        if (MailHasItemsCompat(mail))
        {
            for (MailItemInfoVec::iterator i = mail->items.begin(); i != mail->items.end(); ++i)
            {
                Item* item = MailItemCompat(mailOwner, i->item_guid);
                int count = item ? item->GetCount() : 1;
                ItemPrototype const *proto = GetItemProtoByIdCompat(i->item_template);
                if (proto)
                {
                    sPlayerbotAIConfig.logEvent(ai, "MailAction", proto->Name1, std::to_string(proto->ItemId));
                    out << ChatHelper::formatItem(item, count);
                    if (!mail->subject.empty()) out << " |cffa0a0a0(" << mail->subject << ")";
                }
            }
        }

        out  << ", |cff00ff00" << days << " day(s)";
        tells.push_front(out.str());
        return true;
    }

    bool After(Player* requester, PlayerbotAI* ai) override
    {
        for (std::list<std::string>::iterator i = tells.begin(); i != tells.end(); ++i)
        {
            ai->TellPlayer(requester, *i, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return true;
    }

    static TellMailProcessor instance;

private:
    std::list<std::string> tells;
};

class TakeMailProcessor : public MailProcessor
{
public:

    bool Before(Player* requester, PlayerbotAI* ai) override
    {
        copper = 0;
        items.clear();
        return true;
    }

    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        Player* bot = ai->GetBot();
        if (!GetMailOwner(bot))
            return false;

        if (!CheckBagSpace(bot))
        {
            ai->TellError(requester, "Not enough bag space");
            return false;
        }

        ObjectGuid mailbox = FindMailbox(ai);
        bool moneyTaken = false;
        if (mail->money)
        {
            std::ostringstream out;
            if (event.getSource() == "rpg action")
            {
                copper += mail->money;
            }
            else
            {
                out << mail->subject << ", |cffffff00" << ChatHelper::formatMoney(mail->money) << "|cff00ff00 processed";
                ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            MailTakeMoneyCompat(bot, mailbox, mail);
            moneyTaken = true;
        }

        if (MailHasItemsCompat(mail))
        {
            std::list<uint32> guids;
            for (MailItemInfoVec::iterator i = mail->items.begin(); i != mail->items.end(); ++i)
            {
                ItemPrototype const* proto = GetItemProtoByIdCompat(i->item_template);
                if (proto)
                    guids.push_back(i->item_guid);
            }

            for (std::list<uint32>::iterator i = guids.begin(); i != guids.end(); ++i)
            {
                Item* item = MailItemCompat(GetMailOwner(bot), *i);

                if (item)
                {
                    if (event.getSource() == "rpg action")
                    {
                        items.push_back(ChatHelper::formatItem(item, item->GetCount()));
                    }
                    else
                    {
                        std::ostringstream out;
                        out << mail->subject << ", " << ChatHelper::formatItem(item) << "|cff00ff00 processed";
                        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    }
                }

                MailTakeItemCompat(bot, mailbox, mail, *i);
            }
        }

        // Only delete the mail if both money and items have been taken (or if it was an empty notification mail)
        MasterPlayer* mailOwner = GetMailOwner(bot);
        Mail* currentMail = mailOwner ? MailByIdCompat(mailOwner, mail->messageID) : nullptr;
        if (currentMail && currentMail->money == 0 && currentMail->items.empty() && !ai->HasActivePlayerMaster())
        {
            RemoveMail(bot, mail->messageID, mailbox);
        }
        else if (mail->sender < 10 && !ai->HasActivePlayerMaster() && !mail->money && !MailHasItemsCompat(mail))
        {
            RemoveMail(bot, mail->messageID, mailbox);
        }
        return true;
    }

    bool After(Player* requester, PlayerbotAI* ai) override
    {
        if (!items.empty())
        {
            std::map<std::string, std::string> args;
            args["%itemcount"] = std::to_string(items.size());

            std::vector<std::string> lines = { BOT_TEXT2("|cff00ff00%itemcount items recieved from mail: ", args) };
            for (auto& item : items)
            {
                if (lines.back().size() + item.size() > 256)
                    lines.push_back("");

                lines.back() = lines.back() + item;
            }

            for (auto& line : lines)
                ai->TellPlayer(requester, line, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        if (copper > 0)
        {
            std::map<std::string, std::string> args;
            args["%money"] = ChatHelper::formatMoney(copper);
            ai->TellPlayer(requester, BOT_TEXT2("|cffffff00%money |cff00ff00recieved from mail.", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return true;
    }

    static TakeMailProcessor instance;

private:
    bool CheckBagSpace(Player* bot)
    {
        uint32 totalused = 0, total = 16;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
        {
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                totalused++;
        }
        uint32 totalfree = 16 - totalused;
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            if (const Bag* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
            {
                ItemPrototype const* pBagProto = GetItemProtoCompat(pBag);
                if (pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
                    totalfree += pBag->GetFreeSlots();
            }

        }

        return totalfree >= 2;
    }

private:
    uint32 copper = 0;
    std::vector<std::string> items;
};

// Takes one order item id from waiting mail: fits that single item through
// the core's per-item check (stack merge included) instead of demanding free
// slots for the whole mailbox. A bot whose bags hold nothing sellable still
// lifts its order item this way; the broad take above needs two free slots
// and would refuse forever. Stops at the first item that no longer fits so
// the leg can park instead of looping past unrelated mail.
uint32_t ai::TakeOrderMail(PlayerbotAI* ai, uint32_t itemId)
{
    if (!ai || !itemId)
        return 0;
    Player* bot = ai->GetBot();
    MasterPlayer* owner = GetMailOwner(bot);
    if (!bot || !owner)
        return 0;
    ObjectGuid mailbox = MailProcessor::FindMailbox(ai);
    if (mailbox.IsEmpty())
        return 0;
    uint32_t taken = 0;
    time_t curTime = time(nullptr);
    for (auto itr = MailBeginCompat(owner); itr != MailEndCompat(owner); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!MailHasItemsCompat(mail))
            continue;
        uint32_t wantedGuid = 0;
        uint32_t senderLow = mail->sender;
        for (MailItemInfoVec::const_iterator it = mail->items.begin(); it != mail->items.end(); ++it)
            if (it->item_template == itemId)
            {
                wantedGuid = it->item_guid;
                break;
            }
        if (!wantedGuid)
            continue;
        Item* item = MailItemCompat(owner, wantedGuid);
        if (!item)
            continue;
        uint32_t count = item->GetCount();
        ItemPosCountVec dest;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            break;
        MailTakeItemCompat(bot, mailbox, mail, wantedGuid);
        taken += count;
        if (senderLow)
            TortoiseBots::GuildOrders::NoteTakenMailSender(senderLow, itemId, count);
        Mail* current = MailByIdCompat(owner, mail->messageID);
        if (current && current->money == 0 && current->items.empty() && !ai->HasActivePlayerMaster())
        {
            MailDeleteCompat(bot, mailbox, mail->messageID);
        }
    }
    return taken;
}

namespace
{
// Takes the value one settlement mail carries into the bot's hands: the sale
// gold or the returned stack. False when the mail gave nothing, so the caller
// leaves the row as it was.
bool TakeRelistValue(PlayerbotAI* ai, Player* bot, ObjectGuid mailbox, Mail* mail, uint32_t itemId,
    TortoiseBots::GuildOrders::RelistMailKind kind, uint32_t& gold, uint32_t& items)
{
    if (kind == TortoiseBots::GuildOrders::RelistMailKind::Sold)
    {
        uint32_t before = bot->GetMoney();
        MailTakeMoneyCompat(bot, mailbox, mail);
        uint32_t after = bot->GetMoney();
        if (after <= before)
            return false;
        gold = after - before;
        return true;
    }
    if (kind != TortoiseBots::GuildOrders::RelistMailKind::Expired)
        return false;
    uint32_t itemGuid = 0;
    for (MailItemInfoVec::const_iterator it = mail->items.begin(); it != mail->items.end(); ++it)
    {
        if (it->item_template == itemId)
        {
            itemGuid = it->item_guid;
            break;
        }
    }
    if (!itemGuid)
        return false;
    uint32_t before = ai->GetInventoryItemsCountWithId(itemId);
    MailTakeItemCompat(bot, mailbox, mail, itemGuid);
    uint32_t after = ai->GetInventoryItemsCountWithId(itemId);
    if (after <= before)
        return false;
    items = after - before;
    return true;
}

void DeleteEmptyRelistMail(PlayerbotAI* ai, MasterPlayer* owner, ObjectGuid mailbox,
    uint32_t messageId)
{
    Mail* current = MailByIdCompat(owner, messageId);
    if (current && current->money == 0 && current->items.empty() && !ai->HasActivePlayerMaster())
    {
        MailDeleteCompat(ai->GetBot(), mailbox, messageId);
    }
}
} // namespace

// Settles the guild relist ledger against the waiting auction mail. A sale
// mail pays its gold into the bot's hands, an expired listing returns the
// stack; the ledger row then waits for the bank trip the relist drain makes.
// Runs at any order state, or with no order row at all: the guild-funded
// value outlives the order that posted it. Returns the rows this read matched.
uint32_t ai::TakeRelistMail(PlayerbotAI* ai)
{
    if (!ai)
        return 0;
    Player* bot = ai->GetBot();
    MasterPlayer* owner = GetMailOwner(bot);
    if (!bot || !owner || !bot->GetGuildId())
        return 0;
    std::vector<TortoiseBots::GuildOrders::RelistLedgerEntry> entries =
        TortoiseBots::GuildOrders::RelistEntries(bot->GetGUIDLow());
    if (entries.empty())
        return 0;
    ObjectGuid mailbox = MailProcessor::FindMailbox(ai);
    if (mailbox.IsEmpty())
        return 0;

    using namespace TortoiseBots::GuildOrders;
    uint32_t settled = 0;
    time_t curTime = time(nullptr);

    // A claimed row names the mail it is taking. A mail that is gone or that
    // no longer carries the row's value means the take landed: the value is in
    // the bot, so the row is received and the bank drain settles it. A mail
    // that still carries the value is taken again. A claimed row is never
    // matched by its terms a second time.
    for (size_t i = 0; i < entries.size(); ++i)
    {
        RelistLedgerEntry& entry = entries[i];
        if (entry.state != RelistState::Claimed)
            continue;
        Mail* mail = MailByIdCompat(owner, entry.claimMailId);
        RelistMail view;
        if (mail && mail->state != MAIL_STATE_DELETED)
            view = MakeRelistMailView(mail, owner);
        if (!RelistClaimMailHoldsValue(entry, view))
        {
            if (TortoiseBots::GuildOrders::NoteRelistReceived(bot->GetGUIDLow(), entry.seq,
                    entry.heldGold, entry.heldCount))
            {
                RelistNoteClaimSettled(entry);
                ++settled;
            }
            continue;
        }
        RelistMailKind const kind = view.money ? RelistMailKind::Sold : RelistMailKind::Expired;
        uint32_t gold = 0;
        uint32_t items = 0;
        if (!TakeRelistValue(ai, bot, mailbox, mail, entry.itemId, kind, gold, items))
            continue;
        if (!TortoiseBots::GuildOrders::NoteRelistReceived(bot->GetGUIDLow(), entry.seq, gold, items))
            continue;
        RelistNoteMailReceived(entry, kind, gold, items);
        ++settled;
        DeleteEmptyRelistMail(ai, owner, mailbox, mail->messageID);
    }

    for (auto itr = MailBeginCompat(owner); itr != MailEndCompat(owner); ++itr)
    {
        Mail* mail = *itr;
        if (!mail || mail->state == MAIL_STATE_DELETED || curTime < mail->deliver_time)
            continue;
        if (!mail->money && !MailHasItemsCompat(mail))
            continue;
        // A claimed row already owns this mail: its retry pass takes it.
        size_t claimed = 0;
        if (RelistClaimedIndex(entries, mail->messageID, claimed))
            continue;
        RelistMail view = MakeRelistMailView(mail, owner);
        if (!view.auction)
            continue;
        // A mail settles a row only when its item, listing terms and timing
        // identify a posted row. Look-alike rows of one guild settle as the
        // oldest: that guild is credited either way. Rows of different guilds
        // leave the mail to normal mail handling, because the ledger never
        // pays one guild another auction's value.
        size_t matched = 0;
        size_t const matches = RelistMatchCount(entries, view, uint64_t(curTime), matched);
        if (!matches || !RelistSettleIndex(entries, view, uint64_t(curTime), matched))
        {
            if (matches > 1)
                TB_LOG_BASIC("TortoiseBots: relist mail ambiguous for bot %u (item %u): %zu ledger "
                    "rows match across guilds; leaving it to normal mail handling.", bot->GetGUIDLow(),
                    view.subjectItemId, matches);
            continue;
        }
        RelistLedgerEntry& entry = entries[matched];
        RelistMailKind const kind = RelistMailMatch(entry, view, uint64_t(curTime));

        // Claim before the take: the row names the mail and the value it
        // promises, so a take whose received write fails is still known. A
        // failed claim write leaves the mail untouched for the next read.
        RelistLedgerEntry claim = entry;
        RelistNoteMailClaimed(claim, mail->messageID, view, kind);
        if (!TortoiseBots::GuildOrders::NoteRelistClaimed(bot->GetGUIDLow(), entry.seq,
                claim.claimMailId, claim.heldGold, claim.heldCount))
            continue;
        entry = claim;

        uint32_t gold = 0;
        uint32_t items = 0;
        if (!TakeRelistValue(ai, bot, mailbox, mail, entry.itemId, kind, gold, items))
            continue;
        if (!TortoiseBots::GuildOrders::NoteRelistReceived(bot->GetGUIDLow(), entry.seq, gold, items))
            continue;
        RelistNoteMailReceived(entry, kind, gold, items);
        ++settled;
        DeleteEmptyRelistMail(ai, owner, mailbox, mail->messageID);
    }
    return settled;
}

class DeleteMailProcessor : public MailProcessor
{
public:
    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        std::ostringstream out;
        out << "|cffffffff" << mail->subject << "|cffff0000 deleted";
        RemoveMail(ai->GetBot(), mail->messageID, FindMailbox(ai));
        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    static DeleteMailProcessor instance;
};

class ReadMailProcessor : public MailProcessor
{
public:
    bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) override
    {
        std::ostringstream out, body;
        out << "|cffffffff" << mail->subject;
        ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        if (MailHasTextCompat(mail))
        {
            body << "\n" << MailBodyTextCompat(mail);
            ai->TellPlayer(requester, body.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }
        return true;
    }

    static ReadMailProcessor instance;
};

TellMailProcessor TellMailProcessor::instance;
TakeMailProcessor TakeMailProcessor::instance;
DeleteMailProcessor DeleteMailProcessor::instance;
ReadMailProcessor ReadMailProcessor::instance;

bool MailAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!requester && event.getSource() != "rpg action")
        return false;

    if (!MailProcessor::FindMailbox(ai) && event.getSource() != "debug")
    {
        ai->TellError(requester, "There is no mailbox nearby");
        return false;
    }

    if (processors.empty())
    {
        processors["?"] = &TellMailProcessor::instance;
        processors["take"] = &TakeMailProcessor::instance;
        processors["delete"] = &DeleteMailProcessor::instance;
        processors["read"] = &ReadMailProcessor::instance;
    }

    std::string text = event.GetParam();
    if (text.empty())
    {
        ai->TellPlayer(requester, "whisper 'mail ?' to query mailbox, 'mail take/delete/read filter' to take/delete/read mails by filter");
        return false;
    }

    std::vector<std::string> ss = split(text, ' ');
    std::string action = ss[0];
    std::string filter = ss.size() > 1 ? ss[1] : "";
    MailProcessor* processor = processors[action];
    if (!processor)
    {
        std::ostringstream out; out << action << ": I don't know how to do that";
        ai->TellPlayer(requester, out.str());
        return false;
    }

    // A mail read also settles the guild relist ledger, whatever the bot's
    // order state: the guild-funded value arrives long after the posting.
    if (action == "take")
        ai::TakeRelistMail(ai);

    if (!processor->Before(requester, ai))
        return false;

    MasterPlayer* mailOwner = GetMailOwner(bot);
    if (!mailOwner)
        return false;

    std::vector<Mail*> mailList;
    time_t cur_time = time(0);
    for (auto itr = MailBeginCompat(mailOwner); itr != MailEndCompat(mailOwner); ++itr)
    {
        if ((*itr)->state == MAIL_STATE_DELETED || cur_time < (*itr)->deliver_time)
            continue;

        Mail *mail = *itr;
        mailList.push_back(mail);
    }

    if (mailList.empty())
        return false;

    std::map<int, Mail*> filtered = filterList(mailList, filter);
    for (std::map<int, Mail*>::iterator i = filtered.begin(); i != filtered.end(); ++i)
    {
        if (!processor->Process(requester, i->first, i->second, ai, event))
            break;
    }

    return processor->After(requester, ai);
}

void MailProcessor::RemoveMail(Player* bot, uint32 id, ObjectGuid mailbox)
{
    MailDeleteCompat(bot, mailbox, id);
}

ObjectGuid MailProcessor::FindMailbox(PlayerbotAI* ai)
{
    std::list<ObjectGuid> gos = *ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest game objects no los");
    ObjectGuid mailbox;
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); ++i)
    {
        GameObject* go = ai->GetGameObject(*i);
        if (go && go->GetGoType() == GAMEOBJECT_TYPE_MAILBOX)
        {
            mailbox = go->getObjectGuid();
            break;
        }
    }

    return mailbox;
}