// The Turtle side of the both-core helpers the ForeverCore port added in slice 5 (the plan loader).
// Same names and signatures as forever/src/compat/TbCompat_S5.h; each forwards to the call the loader always made.
// Owned by package P5 of the slice-5 work: no other package edits this file.
#pragma once

#include <memory>
#include <string>

// A world-database read: the core's Query hands over a raw QueryResult* the caller frees (src/shared/Database/Database.h), held by a
// unique_ptr as before (the original `std::unique_ptr<QueryResult> result{WorldDatabase.Query(sql)}`).
inline std::unique_ptr<QueryResult> WorldQueryCompat(char const* sql) { return std::unique_ptr<QueryResult>(WorldDatabase.Query(sql)); }

// The text of a field: the original `field.GetCppString()`.
inline std::string FieldTextCompat(Field& field) { return field.GetCppString(); }
