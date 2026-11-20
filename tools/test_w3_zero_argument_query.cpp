// The zero-argument DbPQueryW3 call must send its text unchanged.
// ForeverCore's half formats with printf; a query with a literal percent and no arguments (LIKE '100%') has no conversion,
// so it must reach the database as written instead of through std::snprintf's format rules (review finding 3).
#include <cstdio>
#include <string>

// the header compiles whole: GuidStringW3 only needs the one member it calls
struct ObjectGuid { std::string ToString() const { return std::string(); } };
#include "../forever/src/compat/TbCompat_W3.h"

namespace
{
struct RecordingDb
{
    std::string sent;

    std::string Query(char const* sql)
    {
        sent = sql;
        return sent;
    }
};

int Fail(char const* what, std::string const& sent, std::string const& want)
{
    std::printf("%s\n  sent: %s\n  want: %s\n", what, sent.c_str(), want.c_str());
    return 1;
}
} // namespace

int main()
{
    RecordingDb db;

    DbPQueryW3(db, "SELECT a FROM b WHERE c LIKE '100%'");
    if (db.sent != "SELECT a FROM b WHERE c LIKE '100%'")
        return Fail("a literal percent with no arguments changed", db.sent, "SELECT a FROM b WHERE c LIKE '100%'");

    DbPQueryW3(db, "SELECT a FROM b");
    if (db.sent != "SELECT a FROM b")
        return Fail("a plain query changed", db.sent, "SELECT a FROM b");

    DbPQueryW3(db, "SELECT '%u' FROM t", 7u);
    if (db.sent != "SELECT '7' FROM t")
        return Fail("a printf conversion was not formatted", db.sent, "SELECT '7' FROM t");

    std::printf("test_w3_zero_argument_query: OK\n");
    return 0;
}
