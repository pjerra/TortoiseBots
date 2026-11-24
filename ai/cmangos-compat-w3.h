// The Turtle side of the both-core helpers the ForeverCore port added in wide pass 3 (branch port/wide-pass-3).
// Same names and signatures as forever/src/compat/TbCompat_W3.h; each is the expression the bot always made (the call is moved,
// not changed). Owned by that pass: no other package edits this file. Notes: forever/docs/WIDE-PASS-R3-NOTES.md.
// Helpers that read a member of a core type are templates, so this header needs no include of its own (the call site already
// had the type complete where it used the member).
#pragma once
#include <string>

// Family 1 (spellings): nothing to define here. `event.getSource()` and `->GetZoneId()` are the spellings the Turtle shim
// (ai/cmangos-compat-shim.h) already maps `GetSource` and `getZoneId` to.

// Family 2 (a guid as text for a log line or a display fallback): the original call.
inline std::string GuidStringW3(ObjectGuid const& guid) { return guid.GetString(); }

// Family 3 (database reads): the original calls. DbPQueryW3 is the printf-format query of a database that has no helper of its
// own (the character database has CharacterPQueryCompat, TbCompat_Unported.h / cmangos-compat-unported.h).
template <class Db, class... Args>
inline auto DbPQueryW3(Db& db, char const* format, Args... args) { return db.PQuery(format, args...); }
template <class Field>
inline std::string FieldTextW3(Field const& field) { return field.GetCppString(); }
template <class Field>
inline float FieldDoubleAsFloatW3(Field const& field) { return field.GetFloat(); }
template <class Field>
inline bool FieldIsNullW3(Field const& field) { return field.IsNULL(); }
template <class Db>
inline void DbEscapeW3(Db& db, std::string& text) { db.escape_string(text); }

// Family 4 (the singletons that are objects here): the original object. Macros, so the call site's own include of the manager is
// the only one needed (the wrapper is included after it, but a helper function would need the type declared at this point).
#define AuctionMgrW3() (sAuctionMgr)
#define AccountMgrW3() (sAccountMgr)
#define ScriptMgrW3() (sScriptMgr)

// Family 5 (quest template): the original members.
template <class Q>
inline std::string const& QuestTitleW3(Q const* quest) { return quest->GetTitle(); }
template <class Q>
inline auto const& QuestRewItemIdsW3(Q const* quest) { return quest->RewItemId; }
template <class Q>
inline auto const& QuestRewItemCountsW3(Q const* quest) { return quest->RewItemCount; }
template <class Q>
inline auto const& QuestRewChoiceItemIdsW3(Q const* quest) { return quest->RewChoiceItemId; }
