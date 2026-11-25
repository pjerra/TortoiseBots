// Standalone test of the PURE half of the recruiter gossip wizard
// (forever/src/host/hire/TbHireRecruiterPure.h; package P3, forever/docs/HIRE-PORT-PLAN.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_hire_recruiter_wizard_check.cpp -o /tmp/tw && /tmp/tw
#include "forever/src/host/hire/TbHireRecruiterPure.h"

#include <cstdio>
#include <set>

namespace W = TortoiseBots::hire::wizard;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // Every page's choice round-trips, including the extreme class/race/spec values.
    {
        uint8_t classId = 0;
        CHECK(W::DecodeClass(W::PackClass(1), classId) && classId == 1);
        CHECK(W::DecodeClass(W::PackClass(11), classId) && classId == 11);
    }
    {
        // Class page: only a real class id is a valid click, and the full 32-bit action is checked.
        uint8_t classId = 0;
        CHECK(!W::DecodeClass(0, classId));
        CHECK(!W::DecodeClass(12, classId));
        CHECK(!W::DecodeClass(255, classId));
        CHECK(!W::DecodeClass(0x0100, classId));
        CHECK(!W::DecodeClass(0xFFFFFFFFu, classId));
    }
    {
        uint8_t classId = 0, race = 0;
        CHECK(W::DecodeClassRace(W::PackClassRace(3, 1), classId, race) && classId == 3 && race == 1);
        CHECK(W::DecodeClassRace(W::PackClassRace(11, 96), classId, race) && classId == 11 && race == 96);
        CHECK(!W::DecodeClassRace(W::PackClassRace(1, 0), classId, race));
        CHECK(!W::DecodeClassRace(W::PackClassRace(1, 97), classId, race));
        CHECK(!W::DecodeClassRace(W::PackClassRace(12, 1), classId, race));
        CHECK(!W::DecodeClassRace(0x010203, classId, race));
    }
    {
        uint8_t classId = 0, race = 0, gender = 0;
        CHECK(W::DecodeClassRaceGender(W::PackClassRaceGender(5, 4, 1), classId, race, gender) &&
            classId == 5 && race == 4 && gender == 1);
        CHECK(W::DecodeClassRaceGender(W::PackClassRaceGender(5, 4, 0), classId, race, gender) && gender == 0);
        CHECK(!W::DecodeClassRaceGender(W::PackClassRaceGender(5, 4, 2), classId, race, gender));
        CHECK(!W::DecodeClassRaceGender(0x01000000u | W::PackClassRaceGender(1, 1, 0), classId, race, gender));
    }
    {
        W::Choice const in{ 11, 96, 1, 3 };
        W::Choice out;
        CHECK(W::PackChoice(in) == ((11u << 24) | (96u << 16) | (1u << 8) | 3u));
        CHECK(W::DecodeChoice(W::PackChoice(in), out) && out.classId == 11 && out.race == 96 &&
            out.gender == 1 && out.specIndex == 3);
        CHECK(!W::DecodeChoice(W::PackChoice({ 11, 96, 1, 4 }), out));
        CHECK(!W::DecodeChoice(W::PackChoice({ 0, 1, 0, 0 }), out));
        CHECK(!W::DecodeChoice(W::PackChoice({ 1, 0, 0, 0 }), out));
        CHECK(!W::DecodeChoice(W::PackChoice({ 1, 1, 2, 0 }), out));
    }

    // "< Back" is the low byte 255 and is never a valid choice on any page, so a stale menu cannot turn a
    // back click into a hire.
    {
        CHECK(W::IsBack(255) && W::IsBack(W::BackFrom(1)) && W::IsBack(W::BackFrom(11, 96)));
        W::Choice out;
        CHECK(!W::DecodeChoice(W::BackFrom(1, 1, 0), out));
        uint8_t classId = 0, race = 0, gender = 0;
        CHECK(!W::DecodeClassRace(W::PackClassRace(1, uint8_t(W::kActionBack)), classId, race));
        CHECK(!W::DecodeClassRaceGender(W::PackClassRaceGender(1, 1, uint8_t(W::kActionBack)), classId, race, gender));
        // No valid choice packs a low byte equal to the back action.
        for (uint8_t spec = 0; spec <= W::kMaxSpecIndex; ++spec)
            for (uint8_t gender = 0; gender <= 1; ++gender)
                CHECK((W::PackChoice({ 11, 96, gender, spec }) & 0xFF) <= W::kMaxSpecIndex);
    }

    // The class menu: the nine hireable classes, all valid, no duplicates, and the faction rule.
    {
        std::set<uint8_t> seen;
        for (uint32_t i = 0; i < W::kHireClassCount; ++i)
        {
            CHECK(W::ValidClass(W::kHireClasses[i]));
            CHECK(seen.insert(W::kHireClasses[i]).second);
        }
        CHECK(W::kHireClassCount == 9);
        CHECK(W::ClassOffered(2, W::OfferSide::Alliance));
        CHECK(!W::ClassOffered(7, W::OfferSide::Alliance));
        CHECK(W::ClassOffered(7, W::OfferSide::Horde));
        CHECK(!W::ClassOffered(2, W::OfferSide::Horde));
        CHECK(W::ClassOffered(2, W::OfferSide::Both) && W::ClassOffered(7, W::OfferSide::Both));
        CHECK(!W::ClassOffered(0, W::OfferSide::Both) && !W::ClassOffered(12, W::OfferSide::Both));
    }

    // The equipment step: level 60 only, one unlocked set at least, tiers on, and never past the sender space.
    {
        CHECK(!W::TierStepVisible(true, 59, 3));
        CHECK(!W::TierStepVisible(true, 60, 0));
        CHECK(W::TierStepVisible(true, 60, 1));
        CHECK(W::TierStepVisible(true, 60, W::kMaxTier));
        CHECK(!W::TierStepVisible(false, 60, 5));
        CHECK(!W::TierStepVisible(true, 60, uint8_t(W::kMaxTier + 1)));
    }
    {
        uint8_t tier = 99;
        CHECK(W::DecodeTierStep(W::TierStepSender(0), tier) && tier == 0);
        CHECK(W::DecodeTierStep(W::TierStepSender(7), tier) && tier == 7);
        CHECK(!W::DecodeTierStep(519, tier) && !W::DecodeTierStep(520 + W::kMaxTier + 1, tier));
        CHECK(!W::DecodeTierConfirm(520, tier));
        CHECK(W::DecodeTierConfirm(W::TierConfirmSender(3), tier) && tier == 3);
        CHECK(!W::DecodeTierStep(W::kSenderTierBack, tier));
        CHECK(!W::DecodeTierConfirm(W::kSenderTierBack, tier));
        CHECK(W::TierStepSender(W::kMaxTier) < W::kSenderTierBack);
        CHECK(W::TierConfirmSender(W::kMaxTier) < W::kSenderTierBack);
    }

    // Sender map: the root is its own value, the wizard steps follow it, and the tier range stays above the
    // contracts range the other pages own (TbHire.h: 501-504, 505-509, 510-512, 520..540).
    {
        CHECK(W::kSenderRoot == 500);
        CHECK(W::kSenderRoot < 501);
        CHECK(W::kSenderRoot + 1 + 3 == 504);       // wizard: 501 + step 0..3
        CHECK(W::TierStepSender(0) == 520);
        CHECK(W::kSenderTierBack == 540);
        CHECK(W::TierConfirmSender(0) > 512);
        CHECK(W::kSenderTierBack > W::TierConfirmSender(W::kMaxTier));
    }

    // Greeting text ids: the recruiter entry, and entry+100 for the HireAllowPvP variant.
    {
        CHECK(W::IsRecruiterEntry(1000000) && W::IsRecruiterEntry(1000054) && W::IsRecruiterEntry(1000099));
        CHECK(!W::IsRecruiterEntry(999999) && !W::IsRecruiterEntry(1000100));
        CHECK(W::GossipTextId(1000000, false) == 1000000);
        CHECK(W::GossipTextId(1000000, true) == 1000100);
        CHECK(W::GossipTextId(1000054, true) == 1000154);
    }

    std::printf("test_hire_recruiter_wizard_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
