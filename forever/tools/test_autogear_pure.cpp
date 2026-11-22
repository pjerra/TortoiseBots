// Pure unit tests for forever/src/host/TbAutogearPure.h.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_autogear_pure.cpp -o /tmp/test_autogear_pure && /tmp/test_autogear_pure
#include "forever/src/host/TbAutogearPure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace A = TortoiseBots::autogear;
namespace T = TortoiseBots;

static int g_checks = 0;
static int g_failed = 0;

static void Check(bool ok, char const* name, int line)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failed;
    std::printf("FAIL %s (line %d)\n", name, line);
}

#define CHECK(name, cond) Check((cond), (name), __LINE__)

int main()
{
    // 1. Owned-bot arg parsing: blue->3, purple->4, 70->ilvl 70, garbage refused, quality clamp 5.
    {
        std::string bot;
        T::OwnedBotAutogearRequest req;

        A::ParseBotAutogearTail("Alty", 2, 0, bot, req);
        CHECK("default empty arg ok", req.ok && bot == "Alty" && req.quality == 2 && req.ilvlCap == 0);

        A::ParseBotAutogearTail("Alty blue", 4, 0, bot, req);
        CHECK("blue -> 3", req.ok && bot == "Alty" && req.quality == 3 && req.ilvlCap == 0);

        A::ParseBotAutogearTail("Alty purple", 5, 0, bot, req);
        CHECK("purple -> 4", req.ok && bot == "Alty" && req.quality == 4 && req.ilvlCap == 0);

        A::ParseBotAutogearTail("Alty purple", 2, 0, bot, req);
        CHECK("purple clamped to cap 2", req.ok && req.quality == 2);

        A::ParseBotAutogearTail("Alty", 9, 0, bot, req);
        CHECK("quality cap clamped to 5", req.ok && req.quality == 5);

        A::ParseBotAutogearTail("Alty 70", 2, 0, bot, req);
        CHECK("70 -> ilvl 70", req.ok && req.ilvlCap == 70 && req.quality == 2);

        A::ParseBotAutogearTail("Alty 70", 2, 60, bot, req);
        CHECK("70 clamped by config ilvl 60", req.ok && req.ilvlCap == 60);

        A::ParseBotAutogearTail("Alty 3", 2, 0, bot, req);
        CHECK("bare digit <= 5 refused", !req.ok);

        A::ParseBotAutogearTail("Alty garbage", 2, 0, bot, req);
        CHECK("garbage refused", !req.ok);
    }

    // 2. Top-level `.autogear` command claim & arg parsing.
    {
        std::string_view rest;
        CHECK("claims autogear", A::ClaimsAutogearCommand("autogear fury", rest) && rest == "fury");
        CHECK("claims AUTOGEAR case-insensitive", A::ClaimsAutogearCommand("  AUTOGEAR", rest) && rest.empty());
        CHECK("does not claim autogear2", !A::ClaimsAutogearCommand("autogear2 fury", rest));

        std::string name, spec;
        uint32_t ilvl = 0;

        CHECK("spec only", A::ParsePlayerAutogearArgs({"fury"}, name, spec, ilvl) && name.empty() && spec == "fury" && ilvl == 0);
        CHECK("player + spec + ilvl", A::ParsePlayerAutogearArgs({"Bob", "prot", "65"}, name, spec, ilvl) &&
                                           name == "Bob" && spec == "prot" && ilvl == 65);
        CHECK("multi-word spec", A::ParsePlayerAutogearArgs({"Bob", "enhancement", "tank", "50"}, name, spec, ilvl) &&
                                     name == "Bob" && spec == "enhancement tank" && ilvl == 50);
        CHECK("ilvl only refused", !A::ParsePlayerAutogearArgs({"70"}, name, spec, ilvl));
    }

    // 3. Permission table for `.autogear`.
    {
        using P = T::PlayerAutogearPermission;
        CHECK("gm gears anyone", T::DecidePlayerAutogearPermission(true, false, false, false) == P::Allowed);
        CHECK("gm gears challenge mode", T::DecidePlayerAutogearPermission(true, false, false, true) == P::Allowed);
        CHECK("non-gm refused when ForAll=0", T::DecidePlayerAutogearPermission(false, false, true, false) == P::GmOnly);
        CHECK("non-gm self allowed when ForAll=1", T::DecidePlayerAutogearPermission(false, true, true, false) == P::Allowed);
        CHECK("non-gm other refused when ForAll=1", T::DecidePlayerAutogearPermission(false, true, false, false) == P::NotSelf);
        CHECK("non-gm challenge mode refused", T::DecidePlayerAutogearPermission(false, true, true, true) == P::ChallengeGmOnly);
    }

    // 4. Target state table for `.autogear`.
    {
        using S = T::PlayerAutogearTargetState;
        CHECK("dead refused", T::DecidePlayerAutogearTargetState(false, false, false) == S::Dead);
        CHECK("shapeshifted refused", T::DecidePlayerAutogearTargetState(true, false, true) == S::Shapeshifted);
        CHECK("combat refused", T::DecidePlayerAutogearTargetState(true, true, false) == S::InCombat);
        CHECK("ready allowed", T::DecidePlayerAutogearTargetState(true, false, false) == S::Ready);
    }

    // 5. Caps and upgrades-only + ammo-aware ranged rules.
    {
        CHECK("caps allow within", A::CandidateFitsAutogearCaps(2, 60, 2, 60));
        CHECK("caps reject higher quality", !A::CandidateFitsAutogearCaps(3, 60, 2, 60));
        CHECK("caps reject higher ilvl", !A::CandidateFitsAutogearCaps(2, 61, 2, 60));
        CHECK("caps ilvl 0 means uncapped", A::CandidateFitsAutogearCaps(2, 100, 2, 0));

        CHECK("empty slot accepts positive score",
              A::ShouldEquipAutogearUpgrade(false, 0, 10, false, T::RangedAmmoKind::Empty, T::RangedAmmoKind::Empty, false, false));
        CHECK("occupied slot rejects equal score",
              !A::ShouldEquipAutogearUpgrade(true, 10, 10, false, T::RangedAmmoKind::Empty, T::RangedAmmoKind::Empty, false, false));
        CHECK("occupied slot accepts higher score",
              A::ShouldEquipAutogearUpgrade(true, 10, 11, false, T::RangedAmmoKind::Empty, T::RangedAmmoKind::Empty, false, false));
        CHECK("ranged dud replaced by weapon with ammo",
              A::ShouldEquipAutogearUpgrade(true, 50, 20, true, T::RangedAmmoKind::Arrow, T::RangedAmmoKind::Bullet, false, true));
    }

    std::printf("test_autogear_pure: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
