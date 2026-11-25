// The naming and room rules of a hire account (host/hire/TbHireAccounts.cpp), core-free so the account guard and
// the deletion guard can be unit-tested on their own (forever/tools/test_hire_lifecycle_pure.cpp).
//
// A hire account is `TBHIRE<nnnn>` with the fixed stamp as its e-mail. The name keeps hire accounts out of the pool's
// `TBPOOL<nnnn>` range and out of any player's name range; the stamp proves the module made the account, because a name
// alone can be created by anybody.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace hire
{
namespace pure
{

inline constexpr char const* kAccountPrefix = "TBHIRE";
inline constexpr char const* kAccountStamp = "TBHIRESTAMP";
inline constexpr std::uint32_t kHighestAccount = 9999;
// One character at a time: one game account carries one bot session, so two live hires cannot share an account.
inline constexpr std::uint32_t kCharactersPerAccount = 1;

// `TBHIRE0001`; numbers are zero-padded to four digits.
inline std::string AccountName(std::uint32_t number)
{
    std::string name(kAccountPrefix);
    std::string digits = std::to_string(number);
    for (std::size_t i = digits.size(); i < 4; ++i)
        name.push_back('0');
    name += digits;
    return name;
}

inline char UpperAscii(char c)
{
    return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
}

inline bool EqualsUpper(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (UpperAscii(a[i]) != UpperAscii(b[i]))
            return false;
    return true;
}

// The number of a `TBHIRE<nnnn>` name, 0 when it is not one (0 is never issued).
inline std::uint32_t AccountNumber(std::string_view name)
{
    std::size_t const prefix = std::char_traits<char>::length(kAccountPrefix);
    if (name.size() != prefix + 4)
        return 0;
    if (!EqualsUpper(name.substr(0, prefix), kAccountPrefix))
        return 0;
    std::uint32_t number = 0;
    for (std::size_t i = prefix; i < name.size(); ++i)
    {
        char const c = name[i];
        if (c < '0' || c > '9')
            return 0;
        number = number * 10 + std::uint32_t(c - '0');
    }
    return number <= kHighestAccount ? number : 0;
}

inline bool IsAccountName(std::string_view name)
{
    return AccountNumber(name) != 0;
}

// The fixed stamp the module writes as the account's e-mail, compared the way `AccountMgr::CreateAccount` stores it
// (upper-cased).
inline bool IsAccountStamp(std::string_view email)
{
    return EqualsUpper(email, kAccountStamp);
}

inline bool HasRoom(std::uint32_t charactersOnAccount)
{
    return charactersOnAccount < kCharactersPerAccount;
}

} // namespace pure
} // namespace hire
} // namespace TortoiseBots
