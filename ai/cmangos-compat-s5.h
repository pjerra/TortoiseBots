// The Turtle side of the both-core helpers the ForeverCore port added in slice 5 (the plan loader).
// Same names and signatures as forever/src/compat/TbCompat_S5.h; each forwards to the call the loader always made.
// Owned by package P5 of the slice-5 work: no other package edits this file.
#pragma once

#include <memory>
#include <string>

// A world-database read: the core's Query hands over a raw QueryResult* the caller frees (src/shared/Database/Database.h), held by a
// unique_ptr as before (the original `std::unique_ptr<QueryResult> result{WorldDatabase.Query(sql)}`).
// `schemaMissing` (ForeverCore's guard, see forever/src/compat/TbCompat_S5.h): the Turtle core logs a missing table itself and answers null, so it is always false here.
inline std::unique_ptr<QueryResult> WorldQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    return std::unique_ptr<QueryResult>(WorldDatabase.Query(sql));
}

// The text of a field: the original `field.GetCppString()`.
inline std::string FieldTextCompat(Field& field) { return field.GetCppString(); }

// The seek_aura reconciler of a plan reload is not registered: on this core BossTacticsActions.cpp registers it at startup, so a null pointer is a link or
// init-order bug and the reload would silently keep stale fight records. Say so.
inline void SeekReconcilerMissingCompat()
{
    sLog.outError("TortoiseBots: the plan reload found no seek_aura reconciler registered (BossTacticsActions.cpp registers it at startup): the fight records are not re-keyed");
}
