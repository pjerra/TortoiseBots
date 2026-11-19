// ForeverCore side of the both-core helpers of wide pass 3 (branch port/wide-pass-3). Owned by that pass: no other package edits
// this file. Turtle side: ai/cmangos-compat-w3.h (same names and signatures). Both are found through ai/playerbot/TbCompatW3.h
// (__has_include), so the two shared shim headers stay untouched. Notes: forever/docs/WIDE-PASS-R3-NOTES.md.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <type_traits>

// =====================================================================================================================
// Family 1: spellings. Nothing to define here: `event.getSource()` is ai::Event's own member, and `GetZoneId()` is ForeverCore's name for
// what the Turtle shim maps `getZoneId` to. The call sites were rewritten to the spelling both cores read.
// =====================================================================================================================

// =====================================================================================================================
// Family 2: a guid as text. T: ObjectGuid::GetString() ("<type> <name> (Entry/Low ...)"). F: ObjectGuid::ToString()
// (ObjectGuid.h:397). 65 of the 66 rewritten sites print the text in a log line or use it as a display name (HireLifecycle.cpp:317),
// where the two spellings of the same guid may differ between the cores. The 66th is PlayerbotRuntimeFacade.cpp TradeKey: an
// in-memory map key (bot + master), never stored or sent; ToString carries the whole guid value, so it is one key per pair.
// =====================================================================================================================
inline std::string GuidStringW3(ObjectGuid const& guid) { return guid.ToString(); }

// =====================================================================================================================
// Family 3: database reads.
// * Query results: the legacy code wrapped the raw QueryResult* in a unique_ptr; ForeverCore's QueryResult is already a shared
//   pointer to the result set, so `result->Fetch()` and `result->NextRow()` work on it directly. The call sites use the existing
//   QueryGuardCompat (TbCompat_Unported.h:418) on both cores.
// * Printf-format queries (findings item 18): ForeverCore's PQuery takes `{}` formats and silently drops printf arguments, so the
//   character database goes through CharacterPQueryCompat (TbCompat_Unported.h:412) and the others through DbPQueryW3: it formats
//   with printf (the Turtle rules, vsnprintf) and sends the text with Query(). String arguments must be `char const*`.
// * Field text and null test: T Field::GetCppString() / IsNULL(); F Field::GetString() / IsNull().
// * Escaping: T db.escape_string(s); F db.EscapeString(s) (the same call TbCompat_Unported.h:421 makes for the character database).
// =====================================================================================================================
template <class... Args>
inline std::string LegacyFormatW3(char const* format, Args... args)
{
    static_assert((!std::is_same_v<std::decay_t<Args>, std::string> && ...), "pass char const* to a printf format");
    int const size = std::snprintf(nullptr, 0, format, args...);
    if (size < 0)
        return std::string();
    std::string out(static_cast<size_t>(size), '\0');
    std::snprintf(out.data(), out.size() + 1, format, args...);
    return out;
}
template <class Db, class... Args>
inline auto DbPQueryW3(Db& db, char const* format, Args... args) { return db.Query(LegacyFormatW3(format, args...).c_str()); }
template <class F>
inline std::string FieldTextW3(F const& field) { return field.GetString(); }
template <class F>
inline bool FieldIsNullW3(F const& field) { return field.IsNull(); }
template <class Db>
inline void DbEscapeW3(Db& db, std::string& text) { db.EscapeString(text); }

// =====================================================================================================================
// Family 4: the singletons. T: `extern AuctionHouseMgr sAuctionMgr;` (an object). F: `#define sAuctionMgr AuctionHouseMgr::instance()`
// (a pointer; AuctionHouseMgr.h:439, AccountMgr.h:105, ScriptMgr.h:1563). The helper is the object on both.
// =====================================================================================================================
#define AuctionMgrW3() (*sAuctionMgr)
#define AccountMgrW3() (*sAccountMgr)
#define ScriptMgrW3() (*sScriptMgr)

// =====================================================================================================================
// Family 5: quest template members the two cores spell differently and lay out the same: the title (T GetTitle, F GetLogTitle,
// QuestDef.h:673) and the reward arrays (T RewItemId/RewItemCount/RewChoiceItemId, F RewardItemId/RewardItemCount/
// RewardChoiceItemId, std::array, QuestDef.h:753-757; 4 reward items and 6 choices on both: QUEST_REWARD_ITEM_COUNT 4,
// QUEST_REWARD_CHOICES_COUNT 6). The objective arrays (ReqItemId and the others) are NOT here: ForeverCore keeps objectives as a list.
// =====================================================================================================================
template <class Q>
inline std::string const& QuestTitleW3(Q const* quest) { return quest->GetLogTitle(); }
template <class Q>
inline auto const& QuestRewItemIdsW3(Q const* quest) { return quest->RewardItemId; }
template <class Q>
inline auto const& QuestRewItemCountsW3(Q const* quest) { return quest->RewardItemCount; }
template <class Q>
inline auto const& QuestRewChoiceItemIdsW3(Q const* quest) { return quest->RewardChoiceItemId; }
