// Standalone test of the Warsong Gulch carrier-aura detection (BG2 live row 4), pure:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_flag_carrier.cpp -o /tmp/test_bg_flag_carrier && /tmp/test_bg_flag_carrier
//
// Live row 4 could not be staged: the module's only aura tool on the test box was `.tb cast <bot> <spell>`, and nothing proved the
// aura sat on the unit, so "the flag carrier is recognised from its aura" was never shown. This test pins the offline half: the
// detection reads the carrier AURAS, with both teams' spell ids as the core defines them, and derives the right aura per side.
//
// Core citations (ForeverCore b441bf74e0, forever/PINNED_CORE):
//   src/server/scripts/Battlegrounds/WarsongGulch/battleground_warsong_gulch.cpp:51-54    SPELL_WARSONG_FLAG = 23333, SPELL_SILVERWING_FLAG = 23335
//   .../battleground_warsong_gulch.cpp:647-648   "SPELL_WARSONG_FLAG = 23333 // Horde flag carried by an Alliance player"
//   .../battleground_warsong_gulch.cpp:706       CarrierSpell(TEAM_ALLIANCE) = SPELL_SILVERWING_FLAG, CarrierSpell(TEAM_HORDE) = SPELL_WARSONG_FLAG
//   .../battleground_warsong_gulch.cpp:925       PickUpFlag: player->CastSpell(player, CarrierSpell(flagTeam), true)  <- the aura the module reads
//   .../battleground_warsong_gulch.cpp:1048      RemoveAurasDueToSpell(CarrierSpell(flagTeam))                        <- dropped, captured, returned
// The two aura sites in the module (pinned by forever/tools/test_bg2_wiring.py):
//   ai/playerbot/strategy/values/EnemyPlayerValue.cpp  candidate.isFlagCarrier = enemy->HasAura(enemyCarrierAura)   (the enemy carrier)
//                                                      victim->HasAura(ownCarrierAura)                            (an enemy hitting OUR carrier)
//   forever/src/compat/TbCompat_BG.h                    BgFlagCarrierCompat walks the match for the side's carrier aura
// The live row still needs a real flag pickup in a match (row 4, PR body): a GM-placed aura is not the core's cast.
#include "ai/playerbot/policy/bg/BgIds.h"

#include <cstdio>
#include <initializer_list>

namespace bg = TortoiseBots::bg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // The core's two carrier spell ids (battleground_warsong_gulch.cpp:51-54, :647-648).
    CHECK(bg::kWsgCarrierAuraWarsong == 23333);
    CHECK(bg::kWsgCarrierAuraSilverwing == 23335);
    CHECK(bg::kWsgCarrierAuraWarsong != bg::kWsgCarrierAuraSilverwing);

    // The direction the core's CarrierSpell (:706) casts: an Alliance player carries the Horde's Warsong flag, a Horde player the
    // Alliance's Silverwing flag. A swap here is exactly what makes a carrier read on the wrong side.
    CHECK(bg::WsgCarrierAuraOf(bg::BgSide::Alliance) == 23333);
    CHECK(bg::WsgCarrierAuraOf(bg::BgSide::Horde) == 23335);

    // Only the two carried auras count; the dropped-flag spells (23334, 23336) and any other id do not.
    CHECK(bg::IsWsgCarrierAura(23333));
    CHECK(bg::IsWsgCarrierAura(23335));
    CHECK(!bg::IsWsgCarrierAura(23334));   // SPELL_WARSONG_FLAG_DROPPED, at :52
    CHECK(!bg::IsWsgCarrierAura(23336));   // SPELL_SILVERWING_FLAG_DROPPED, at :54
    CHECK(!bg::IsWsgCarrierAura(0));

    // From a bot's side: the enemy it sees carries the bot's OWN flag; the bot's carrier carries the enemy's flag. Both are
    // carrier auras, and the two are never the same id on one side (so "enemy carrier" and "our carrier" cannot read alike).
    for (bg::BgSide botSide : { bg::BgSide::Alliance, bg::BgSide::Horde })
    {
        std::uint32_t const enemy = bg::WsgEnemyCarrierAura(botSide);
        std::uint32_t const own = bg::WsgOwnCarrierAura(botSide);
        CHECK(bg::IsWsgCarrierAura(enemy));
        CHECK(bg::IsWsgCarrierAura(own));
        CHECK(enemy != own);
        // The enemy carrier fights for the other side, so its aura is the other side's carrier aura.
        CHECK(enemy == bg::WsgCarrierAuraOf(bg::BgOtherSide(botSide)));
        // The bot's own carrier fights for the bot's side.
        CHECK(own == bg::WsgCarrierAuraOf(botSide));
    }
    // The concrete case of the row: an Alliance bot's enemy is a Horde player carrying the ALLIANCE flag (Silverwing 23335), and the
    // Alliance's own carrier holds the Horde flag (Warsong 23333); a Horde bot reads the mirror image.
    CHECK(bg::WsgEnemyCarrierAura(bg::BgSide::Alliance) == 23335);
    CHECK(bg::WsgOwnCarrierAura(bg::BgSide::Alliance) == 23333);
    CHECK(bg::WsgEnemyCarrierAura(bg::BgSide::Horde) == 23333);
    CHECK(bg::WsgOwnCarrierAura(bg::BgSide::Horde) == 23335);

    // The other side is an involution, and the four derived values cover both ids exactly.
    CHECK(bg::BgOtherSide(bg::BgOtherSide(bg::BgSide::Alliance)) == bg::BgSide::Alliance);
    CHECK(bg::BgOtherSide(bg::BgOtherSide(bg::BgSide::Horde)) == bg::BgSide::Horde);
    CHECK(bg::BgOtherSide(bg::BgSide::Alliance) == bg::BgSide::Horde);
    CHECK(bg::BgOtherSide(bg::BgSide::Horde) == bg::BgSide::Alliance);
    CHECK(bg::WsgCarrierAuraOf(bg::BgSide::Alliance) + bg::WsgCarrierAuraOf(bg::BgSide::Horde) == 23333u + 23335u);

    std::printf("test_bg_flag_carrier: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
