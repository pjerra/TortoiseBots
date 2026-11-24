
#include "playerbot/playerbot.h"
#include "TellReputationAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TbCompatPC5.h"

using namespace ai;

bool TellReputationAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (!requester)
        return false;

    ObjectGuid selection = requester->GetSelectionGuid();
    if (selection.IsEmpty())
        return false;

    Unit* unit = UnitInMapCompat(requester->GetMap(), selection);
    if (!unit)
        return false;

    const FactionTemplateEntry *factionTemplate = sServerFacade.GetFactionTemplateEntry(unit);
    if (!factionTemplate)
        return false;

    uint32 faction = FactionOfTemplateCompat(factionTemplate);
    const FactionEntry* entry = FactionEntryOfCompat(faction);
    if (!entry)
        return false;

    int32 reputation = bot->GetReputationMgr().GetReputation(faction);

    std::ostringstream out;
    out << FactionNameCompat(entry, LOCALE_enUS) << ": ";
    out << "|cff";
    ReputationRank rank = bot->GetReputationMgr().GetRank(entry);
    switch (rank) {
        case REP_HATED:
            out << "cc2222hated";
            break;
        case REP_HOSTILE:
            out << "ff0000hostile";
            break;
        case REP_UNFRIENDLY:
            out << "ee6622unfriendly";
            break;
        case REP_NEUTRAL:
            out << "ffff00neutral";
            break;
        case REP_FRIENDLY:
            out << "00ff00friendly";
            break;
        case REP_HONORED:
            out << "00ff88honored";
            break;
        case REP_REVERED:
            out << "00ffccrevered";
            break;
        case REP_EXALTED:
            out << "00ffffexalted";
            break;
        default:
            out << "808080unknown";
            break;
    }

    out << "|cffffffff";

    int32 base = 0;
    int32 points = 0;
    ReputationRankSpanCompat(rank, base, points);

    out << " (" << (reputation - base) << "/" << points << ")";
    ai->TellPlayer(requester, out);

    return true;
}
