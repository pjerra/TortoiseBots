#include "../ai/playerbot/policy/class/ShamanTankPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TankChainLightningShouldFire;
using ai::TankEarthPickReplaceable;
using ai::TankFightTargetIsBossOrElite;
using ai::TankFireNovaPulseShouldFire;
using ai::TankFireNovaShouldDrop;
using ai::TankHardCastAllowed;
using ai::TankMagmaPulseShouldDrop;
using ai::TankMagmaShouldFollow;
using ai::TankShamanTargetIsElite;
using ai::TankShamanEarthShockFreeFire;
using ai::TankShamanPullManaReady;
using ai::TankShamanPullWantsChainLightning;
using ai::TankShamanThreatNeedsShock;
using ai::TankShamanWantsWaterShield;
using ai::TankStoneclawShouldDrop;
using ai::TankStrengthOfEarthShouldDrop;

int main()
{
    std::cout << "Starting shaman tank policy tests...\n";

    // Pull mana gate: no pull below 80% own mana; non-mana bots exempt.
    CHECK(TankShamanPullManaReady(true, 100) == true);
    CHECK(TankShamanPullManaReady(true, 80) == true);
    CHECK(TankShamanPullManaReady(true, 79) == false);
    CHECK(TankShamanPullManaReady(true, 0) == false);
    CHECK(TankShamanPullManaReady(false, 0) == true);
    std::cout << "  [PASS] pull waits for 80% own mana\n";

    // Shield swap: Lightning Shield on pull, Water Shield at ~66% mana.
    // Before Water Shield is trained there is nothing to swap to.
    CHECK(TankShamanWantsWaterShield(true, 66) == true);
    CHECK(TankShamanWantsWaterShield(true, 30) == true);
    CHECK(TankShamanWantsWaterShield(true, 67) == false);
    CHECK(TankShamanWantsWaterShield(true, 100) == false);
    CHECK(TankShamanWantsWaterShield(false, 10) == false);
    std::cout << "  [PASS] water shield takes over at two thirds mana\n";

    // Hard-cast gate: allowed when 0 attackers, forbidden when 1+ mobs on the tank.
    CHECK(TankHardCastAllowed(0) == true);
    CHECK(TankHardCastAllowed(1) == false);
    CHECK(TankHardCastAllowed(2) == false);
    CHECK(TankHardCastAllowed(5) == false);
    std::cout << "  [PASS] hard-casts forbidden once mobs hit the tank\n";

    // Chain Lightning: pull tool for 2+ mobs and fine while free of attackers,
    // blocked once mobs are on the tank.
    CHECK(TankChainLightningShouldFire(0, 2) == true);
    CHECK(TankChainLightningShouldFire(0, 5) == true);
    CHECK(TankChainLightningShouldFire(0, 1) == false);
    CHECK(TankChainLightningShouldFire(0, 0) == false);
    CHECK(TankChainLightningShouldFire(1, 2) == false);
    CHECK(TankChainLightningShouldFire(2, 2) == false);
    CHECK(TankChainLightningShouldFire(5, 5) == false);
    CHECK(TankChainLightningShouldFire(0, 2) == true);
    CHECK(TankChainLightningShouldFire(2, 2) == false);
    CHECK(TankShamanPullWantsChainLightning(1) == false);
    CHECK(TankShamanPullWantsChainLightning(2) == true);
    std::cout << "  [PASS] chain lightning fires only when not being hit and pack >= 2\n";

    // Earth Shock fight gate: bosses/elites/raid instances burn it on
    // cooldown; normal 5-man trash stays economical.
    CHECK(TankShamanEarthShockFreeFire(true, false, false) == true);
    CHECK(TankShamanEarthShockFreeFire(false, true, true) == true);
    CHECK(TankShamanEarthShockFreeFire(false, false, false) == false);
    CHECK(TankShamanEarthShockFreeFire(false, true, false) == false);
    CHECK(TankShamanEarthShockFreeFire(false, false, true) == true);
    CHECK(TankShamanEarthShockFreeFire(false, true, true) == true);
    std::cout << "  [PASS] earth shock free-fires on fights that need it\n";

    // Elite-anywhere rank read (live fix): any rank above normal counts,
    // wherever the fight happens. Rank numbers follow the core
    // CreatureEliteType order (normal 0, elite 1, rare-elite 2, world
    // boss 3, rare 4).
    CHECK(TankShamanTargetIsElite(0, 0) == false);
    CHECK(TankShamanTargetIsElite(1, 0) == true);
    CHECK(TankShamanTargetIsElite(2, 0) == true);
    CHECK(TankShamanTargetIsElite(3, 0) == true);
    CHECK(TankShamanTargetIsElite(4, 0) == true);
    std::cout << "  [PASS] open-world elites count as fight targets\n";

    // Stable earth pick (live fix): fights hold Strength of Earth, trash
    // holds Stoneclaw. Never Stoneskin on a fight. One pick owns the slot:
    // the fight pick replaces our own Stoneclaw from the pack just fought,
    // and a standing Strength of Earth never keeps a pack out of Stoneclaw.
    CHECK(TankStrengthOfEarthShouldDrop(true, false, false) == true);
    CHECK(TankStrengthOfEarthShouldDrop(true, true, true) == true);
    CHECK(TankStrengthOfEarthShouldDrop(true, true, false) == false);
    CHECK(TankStrengthOfEarthShouldDrop(false, false, false) == false);
    CHECK(TankStrengthOfEarthShouldDrop(false, true, true) == false);
    CHECK(TankEarthPickReplaceable(true, false) == true);
    CHECK(TankEarthPickReplaceable(false, true) == true);
    CHECK(TankEarthPickReplaceable(true, true) == true);
    CHECK(TankEarthPickReplaceable(false, false) == false);
    std::cout << "  [PASS] one earth totem per fight, never stoneskin\n";

    // Threat-lead decision: shock when a member catches up (70%+ of our
    // threat) or a mob is loose; quiet otherwise. Zero own threat with a
    // member on the list means we already lost it.
    CHECK(TankShamanThreatNeedsShock(100.0f, 69.0f, 70, false) == false);
    CHECK(TankShamanThreatNeedsShock(100.0f, 70.0f, 70, false) == true);
    CHECK(TankShamanThreatNeedsShock(100.0f, 120.0f, 70, false) == true);
    CHECK(TankShamanThreatNeedsShock(100.0f, 0.0f, 70, true) == true);
    CHECK(TankShamanThreatNeedsShock(0.0f, 10.0f, 70, false) == true);
    CHECK(TankShamanThreatNeedsShock(0.0f, 0.0f, 70, false) == false);
    std::cout << "  [PASS] threat lead gates economical earth shock\n";

    // Tank Stoneclaw: grouped only, threat-needed or wide pack, empty earth
    // slot. Solo bots keep the panic rule; manual orders always win.
    // Fights (boss/elite/raid) hold Strength of Earth, so Stoneclaw is suppressed.
    CHECK(TankStoneclawShouldDrop(false, false, false, false, true, 3) == false);
    CHECK(TankStoneclawShouldDrop(true, false, true, false, true, 3) == false);
    CHECK(TankStoneclawShouldDrop(true, true, false, true, false, 1) == false);
    CHECK(TankStoneclawShouldDrop(true, true, false, false, false, 1) == true);
    CHECK(TankStoneclawShouldDrop(true, true, false, false, false, 1, true) == true);
    CHECK(TankStoneclawShouldDrop(true, false, false, true, true, 5) == false);
    CHECK(TankStoneclawShouldDrop(true, false, false, false, true, 1) == true);
    CHECK(TankStoneclawShouldDrop(true, false, false, false, true, 1, true) == false);
    CHECK(TankStoneclawShouldDrop(true, false, false, false, false, 2) == false);
    CHECK(TankStoneclawShouldDrop(true, false, false, false, false, 3) == true);
    CHECK(TankStoneclawShouldDrop(true, false, false, false, false, 3, true) == false);
    // A replaceable standing pick (our own Strength of Earth or Earthbind)
    // does not keep the pack out of Stoneclaw — the pack tool would never
    // land otherwise on a trash pull inside the previous totem's lifetime.
    CHECK(TankStoneclawShouldDrop(true, false, false, true, false, 3, false, true) == true);
    CHECK(TankStoneclawShouldDrop(true, false, false, true, true, 1, false, true) == true);
    // Replacing still needs a pack reason, and never happens on a fight.
    CHECK(TankStoneclawShouldDrop(true, false, false, true, false, 2, false, true) == false);
    CHECK(TankStoneclawShouldDrop(true, false, false, true, true, 5, true, true) == false);
    std::cout << "  [PASS] stoneclaw drops on arrival when threat needs it\n";

    // Fire slot: Fire Nova owns it whenever the drop is available and no Nova
    // totem stands; a standing Nova totem must be left to detonate.
    CHECK(TankFireNovaShouldDrop(true, true, false) == true);
    CHECK(TankFireNovaShouldDrop(true, true, true) == false);
    CHECK(TankFireNovaShouldDrop(true, false, false) == false);   // 15 s category cooldown
    CHECK(TankFireNovaShouldDrop(false, false, false) == false);  // not trained
    std::cout << "  [PASS] nova owns the fire slot while ready and nothing stands\n";

    // Magma order: only the fallback for the Nova cooldown, into a real pack
    // with mana to burn — never while Nova is ready (that is Nova's tick) and
    // never over a standing Nova totem (it would unsummon it before the blast).
    CHECK(TankMagmaShouldFollow(true, false, 5, true) == false);
    CHECK(TankMagmaShouldFollow(false, true, 5, true) == false);
    CHECK(TankMagmaShouldFollow(false, false, 2, true) == false);
    CHECK(TankMagmaShouldFollow(false, false, 3, true) == true);
    CHECK(TankMagmaShouldFollow(false, false, 5, true) == true);
    CHECK(TankMagmaShouldFollow(false, false, 5, false) == false);
    std::cout << "  [PASS] magma stays the fallback on the nova cooldown\n";

    // Forever: the burst needs the standing totem and its own cooldown; the
    // totem stays up, so a standing one is the requirement, not a veto.
    CHECK(TankFireNovaPulseShouldFire(true, true, true) == true);
    CHECK(TankFireNovaPulseShouldFire(true, true, false) == false);
    CHECK(TankFireNovaPulseShouldFire(true, false, true) == false);   // 10 s category cooldown
    CHECK(TankFireNovaPulseShouldFire(false, true, true) == false);   // not trained
    std::cout << "  [PASS] the burst fires around a standing fire totem\n";

    // Forever: Magma is the AoE fire totem the burst pulses from, so a real
    // pack earns it with mana to burn regardless of the burst cooldown.
    CHECK(TankMagmaPulseShouldDrop(2, true) == false);
    CHECK(TankMagmaPulseShouldDrop(3, true) == true);
    CHECK(TankMagmaPulseShouldDrop(5, true) == true);
    CHECK(TankMagmaPulseShouldDrop(5, false) == false);
    std::cout << "  [PASS] magma drops into a real pack with mana\n";

    // Stable fight decision: an engaged elite keeps the fight a fight while
    // the tank switches between it and its normal-rank adds.
    CHECK(TankFightTargetIsBossOrElite(true, false) == true);
    CHECK(TankFightTargetIsBossOrElite(false, true) == true);
    CHECK(TankFightTargetIsBossOrElite(false, false) == false);
    std::cout << "  [PASS] fight decision survives a target switch\n";

    // No flap on a mixed pull: with the stable fight decision the earth pick
    // stays Strength of Earth while the elite is engaged, even though the
    // current target alternates between the elite and its normal-rank adds
    // every step (the old current-target-only read swapped the totem each
    // switch: Stoneclaw took the slot, the fight row took it back).
    {
        int slot = 0;      // 0 empty, 1 Strength of Earth, 2 Stoneclaw
        int replaced = 0;  // drops that replaced a standing pick
        for (int step = 0; step < 6; ++step)
        {
            bool const targetElite = (step % 2) == 0;
            bool const attackerElite = true;  // the elite is engaged throughout
            bool const fight = TankFightTargetIsBossOrElite(targetElite, attackerElite);
            bool const strengthStanding = slot == 1;
            bool const stoneclawStanding = slot == 2;

            bool const dropStrength = TankStrengthOfEarthShouldDrop(fight, slot != 0, stoneclawStanding);
            bool const dropStoneclaw = TankStoneclawShouldDrop(true, false, false, slot != 0, true, 3,
                fight, TankEarthPickReplaceable(strengthStanding, false));

            int const before = slot;
            if (dropStrength)
                slot = 1;
            else if (dropStoneclaw)
                slot = 2;

            if (slot != before && before != 0)
                ++replaced;
        }
        CHECK(slot == 1);       // the fight pick holds for the whole pull
        CHECK(replaced == 0);   // nothing replaced it after the first drop
    }
    std::cout << "  [PASS] earth pick does not flap on a mixed pull\n";

    std::cout << "All shaman tank policy checks PASSED!\n";
    return 0;
}
