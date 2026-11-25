#include "host/hire/TbHireAccounts.h"
#include "host/hire/TbHireAccountsPure.h"

#include "TbBotMgr.h"

#include "compat/TbCompat_WL0.h"

#include "AccountMgr.h"
#include "Log.h"
#include "Random.h"
#include "World.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace hire
{
namespace
{
char const* const kPasswordAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
constexpr std::size_t kPasswordLength = 16;
// A hire's character is created a few ticks after its account is handed out; the reservation only has to cover that
// window and a failed hire, so an account is offered again after this long at the latest.
constexpr std::chrono::minutes kReservationLifetime{ 10 };
// Names are handed out from 1 upward and an empty one is reused, so the walk stops at the first empty account:
// its length is the peak number of live hires, never the account range.
constexpr std::uint32_t kAccountProbes = pure::kHighestAccount;

std::string RandomPassword()
{
    std::string password;
    password.reserve(kPasswordLength);
    std::size_t const alphabet = std::char_traits<char>::length(kPasswordAlphabet);
    for (std::size_t i = 0; i < kPasswordLength; ++i)
        password.push_back(kPasswordAlphabet[urand(0, std::uint32_t(alphabet - 1))]);
    return password;
}

using Clock = std::chrono::steady_clock;
std::unordered_map<std::uint32_t, Clock::time_point> g_reserved;

void ForgetExpiredReservations(Clock::time_point now)
{
    for (auto it = g_reserved.begin(); it != g_reserved.end();)
    {
        // A hire character now exists, or a session is live: the account is in use, not reserved. It is skipped by the
        // room check while that character exists and offered again as soon as it is gone.
        if (now - it->second > kReservationLifetime || !HireAccountHasRoom(it->first) ||
            BotMgr::Instance().AccountHasBot(it->first) || sWorld->FindSession(it->first))
        {
            it = g_reserved.erase(it);
            continue;
        }
        ++it;
    }
}

bool Reserved(std::uint32_t accountId)
{
    return g_reserved.find(accountId) != g_reserved.end();
}

void Reserve(std::uint32_t accountId)
{
    g_reserved[accountId] = Clock::now();
}

bool Usable(std::uint32_t accountId)
{
    return accountId && IsHireAccount(accountId) && HireAccountHasRoom(accountId) &&
        !BotMgr::Instance().AccountHasBot(accountId) && !sWorld->FindSession(accountId) && !Reserved(accountId);
}

} // namespace

bool IsHireAccount(std::uint32_t accountId)
{
    if (!accountId)
        return false;
    std::string name;
    if (!AccountMgr::GetName(accountId, name) || !pure::IsAccountName(name))
        return false;
    std::string email;
    if (!AccountMgr::GetEmail(accountId, email))
        return false;
    return pure::IsAccountStamp(email);
}

bool HireAccountHasRoom(std::uint32_t accountId)
{
    if (!accountId)
        return false;
    std::vector<PoolCoreCharacter> characters;
    if (!PoolAccountCharactersCompat(accountId, characters))
        return false;
    return pure::HasRoom(std::uint32_t(characters.size()));
}

std::uint32_t AcquireHireAccount(std::string& error)
{
    error.clear();
    ForgetExpiredReservations(Clock::now());

    for (std::uint32_t attempt = 0; attempt < kAccountProbes; ++attempt)
    {
        std::uint32_t const number = attempt + 1;
        if (number > pure::kHighestAccount)
            break;
        std::string const username = pure::AccountName(number);
        std::uint32_t const existing = AccountMgr::GetId(username);

        if (existing)
        {
            if (Usable(existing))
            {
                Reserve(existing);
                TC_LOG_INFO("tortoisebots", "TortoiseBots: hire account {} (#{}) reused", username, existing);
                return existing;
            }
            continue;
        }

        // No account of that name: create it (the next one in the sequence). A name somebody else made in between is
        // adopted only when its stamp proves it is the module's; otherwise the next number is tried.
        std::string password = RandomPassword();
        AccountOpResult const result = sAccountMgr->CreateAccount(username, password, pure::kAccountStamp);
        std::fill(password.begin(), password.end(), '\0');

        std::uint32_t const created = AccountMgr::GetId(username);
        if (result == AccountOpResult::AOR_OK)
        {
            if (!created || !IsHireAccount(created))
            {
                error = "the hire account " + username + " was created but cannot be read back";
                return 0;
            }
            Reserve(created);
            TC_LOG_INFO("tortoisebots", "TortoiseBots: hire account {} (#{}) created", username, created);
            return created;
        }
        if ((result == AccountOpResult::AOR_NAME_ALREADY_EXIST || result == AccountOpResult::AOR_DB_INTERNAL_ERROR) &&
            Usable(created))
        {
            Reserve(created);
            TC_LOG_INFO("tortoisebots", "TortoiseBots: hire account {} (#{}) adopted", username, created);
            return created;
        }
        // Any other failure (a login database that will not write, a core that refuses accounts) is not retried into a
        // loop: the hire is refused before it is charged.
        error = "AccountMgr::CreateAccount(" + username + ") failed with result " + std::to_string(std::uint32_t(result));
        return 0;
    }

    error = "no free hire account (TBHIRE0001-9999) came up";
    return 0;
}

} // namespace hire
} // namespace TortoiseBots
