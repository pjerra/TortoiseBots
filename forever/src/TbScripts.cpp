#include "host/TbOnyxia.h"
// Script registration for the module: the world tick, the config load and the `.tb` chat commands.
#include "TbBotMgr.h"
#include "TbDashboard.h"
#include "dash/TbDashRuns.h"
#include "host/bg/TbBgMapData.h"
#include "host/bg/TbBgQueue.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/pool/TbPoolCreate.h"
#include "host/pool/TbPoolRegistry.h"
#include "host/pool/TbPoolReset.h"
#include "host/pool/TbPoolScheduler.h"
#include "host/pool/TbPoolSpawn.h"
#include "host/cmd/TbCmdOrders.h"
#include "host/cmd/TbPlace.h"
#include "host/cmd/TbCmdFeeds.h"
#include "host/cmd/TbCmdCombat.h"
#include "host/cmd/TbCmdTrace.h"
#include "host/run/TbPlanCommands.h"
#include "host/run/TbRunCommands.h"
#include "host/hire/TbHireWorldBuffs.h"
#include "host/TbPet.h"
#include "host/TbAddonAi.h"
#include "host/hire/TbHireRecruiter.h"
#include "compat/TbItemPrototype.h"
#ifdef TORTOISEBOTS_SLICE1      // the engine's unported-feature counters exist only in the build that has the engine (R9)
#include "host/TbPetFactory.h"
#define TB_UNPORTED_TABLE_ONLY 1   // the table of unported features without the helpers that need the core's types (R6A, R6B)
#include "compat/TbCompat_Unported.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostAddonHint.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostFishing.h"
#include "ai/playerbot/DungeonPlanMgr.h"
#include "runtime/DecisionCapture.h"
#include "runtime/RunTrace.h"
#include "runtime/WhyTrace.h"
#include "host/run/TbPlanUnits.h"
#include "host/run/TbRunHooks.h"
#include "host/run/TbRunLiquid.h"
#include "host/gear/TbGearGen.h"
#include "host/TbSpecEnchant.h"
#include "host/gear/TbGearSourceTier.h"
#include "host/hire/TbHireTiers.h"
#include "host/hire/TbHire.h"
#include "host/hire/TbHireProvision.h"
#include "host/TbHostProfession.h"
#include "host/TbHostWorldPvp.h"
#include "host/TbHostRitualFill.h"
#include "runtime/PassingBuffsService.h"
#include "runtime/HireLifecycle.h"
#if defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
#include "host/TbLuaBots.h"
#endif
#endif
#include "compat/TbSpellEntry.h"
#include "AccountMgr.h"
#include "Config.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Unit.h"
#include "WorldSession.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include <algorithm>
#include <bitset>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <chrono>
#include "Loot.h"
#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ChatCommand.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include "DB2Stores.h"
#include "UpdateTime.h"
#include "Trainer.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "SpellHistory.h"
#include "SpellPackets.h"
#include "TbBotSession.h"
#include "host/TbEngineBrain.h"
#include "host/TbSpellDelayPure.h"
#include "host/TbResistGear.h"
#include "host/TbAutoTrain.h"
#include "host/TbSchool.h"
#include "host/TbAutoSkillSpells.h"
#include "host/TbClassQuestSpells.h"
#include "host/TbTalents.h"
#include "host/TbAutoProfs.h"
#include "host/TbAutoTools.h"
#include "host/TbAutoHunterPet.h"
#include "host/TbAutoFood.h"
#include "host/TbRaidConsumables.h"
#include "host/TbMountShop.h"
#include "host/TbH3.h"
#include "host/TbRaidLayout.h"
#include "host/TbH4.h"
#include "host/TbCc.h"
#include "host/TbThrown.h"
#include "host/cmd/TbTestTargetPure.h"
#include "host/TbIgnore.h"
#include "host/TbLosQuery.h"     // #308: `.tb los <a> <b> [map=<id>]`, the server line-of-sight query
#include "host/TbChatLinkCmd.h"
#include "host/TbHostLfgFill.h"
#include "host/chat/TbChatCmd.h"
#include "host/chat/TbChatIn.h"
#include "host/TbAddonChannel.h"
#include "host/TbAddonInfo.h"
#include "host/TbBotPerfPure.h"
#include "host/TbAddonActions.h"
#include "host/TbAddonTotem.h"
#include "host/TbAddonVerbs.h"
#include "host/TbAddonRoster.h"
#include "host/TbAddonBlessing.h"
#include "host/TbAddonChoice.h"
#include "host/TbAddonInv.h"
#include "host/TbAddonBehaviorService.h"
#include "host/TbAddonTrade.h"
#include "host/TbAddonProfessions.h"
#include "host/TbAddonLootLog.h"
#include "host/TbAddonOrders.h"
#include "host/player/TbPlayerCmd.h"
#include "host/TbAutogear.h"

using namespace Trinity::ChatCommands;
using TortoiseBots::BotMgr;

namespace
{
namespace target = TortoiseBots::h3::target;

// The words of a free-form tail (the splitter `.tb enter`, `.tb hurt` and `.tb power` parse their optional name with).
std::vector<std::string> WordsOf(Tail const& args)
{
    std::vector<std::string> words;
    std::string_view const text(args);
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        size_t const start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i > start)
            words.emplace_back(text.substr(start, i - start));
    }
    return words;
}

ObjectGuid GuidByName(std::string name)
{
    if (!normalizePlayerName(name))
        return ObjectGuid::Empty;
    return sCharacterCache->GetCharacterGuidByName(name);
}

// The word `.tb castlog` prints for a queued cast result.
char const* CastResultKindName(TortoiseBots::CastResult::Kind kind)
{
    switch (kind)
    {
        case TortoiseBots::CastResult::Kind::SpellFailure: return "SpellFailure";
        case TortoiseBots::CastResult::Kind::CastFailed: return "CastFailed";
        case TortoiseBots::CastResult::Kind::SpellDelayed: return "SpellDelayed";
    }
    return "Unknown";
}

// Tick cost window (`.tb perf`): the world's own update time (the previous update's duration, as `server info` prints it, sampled once per tick)
// and the module's share of it (BotMgr::Update: bot sessions plus the engines), average and worst since the last `.tb perf`.
struct PerfWindow
{
    uint64 ticks = 0;
    uint64 worldSumMs = 0;
    uint32 worldMaxMs = 0;
    uint64 botSumUs = 0;
    uint32 botMaxUs = 0;
    // Tick budget: the effective allocations of the last pass, the turns the
    // pool pass served against those it could cut, and how often it stopped.
    uint64 poolBotsSum = 0;
    uint64 processedSum = 0;
    uint64 cutTicks = 0;
    uint64 heldTicks = 0;
    uint32 lastBudgetUs = 0;
    uint32 lastPoolUs = 0;
    uint32 lastCombatUs = 0;
    uint32 targetMs = 0;
    double smoothedMs = 0.0;
    void Reset() { *this = PerfWindow(); }
};
PerfWindow& Perf()
{
    static PerfWindow window;
    return window;
}

// The BOTPERF line's own window: independent of `.tb perf` (which resets on read), reset when the line is written.
TortoiseBots::perf::Window& BotPerf()
{
    static TortoiseBots::perf::Window window;
    return window;
}

// Loads the engine's config and says how long it took (the whole read happens on the world thread).
void LoadEngineConfig()
{
    auto const started = std::chrono::steady_clock::now();
    std::string const error = TortoiseBots::engine::EnsureConfig();
    uint32 const ms = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    if (error.empty())
        TC_LOG_INFO("server.loading", "TortoiseBots: engine configuration loaded in {} ms", ms);
    else
        TC_LOG_WARN("server.loading", "TortoiseBots: engine configuration not loaded ({}); the next engine start tries again", error);
}

class tortoisebots_world : public WorldScript
{
public:
    tortoisebots_world() : WorldScript("tortoisebots_world") { }

    void OnConfigLoad(bool reload) override
    {
        BotMgr::Instance().LoadConfig();
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
        // TBF-B1 third review finding 8: available() follows TortoiseBots.Enable (an atomic word on a config load, so map threads
        // only read it), and a shutdown stays final.
        TortoiseBots::luabots::SetEnabled(BotMgr::Instance().Enabled());
#endif
        TortoiseBots::Dashboard::LoadConfig();
        TortoiseBots::DashRuns::LoadConfig();
        TortoiseBots::pool::LoadConfig();      // W0: the pool key, its rate and the registry rows (re-read on a reload)
        TortoiseBots::pool::PoolScheduler::LoadConfig();   // W0-B: the scheduler's target, window, session and offline times
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        TortoiseBots::poolspawn::LoadConfig(); // the spawn kit key (the start-level range is PlayerbotAIConfig's)
#endif
        TortoiseBots::bg::LoadConfig();        // BG1: the battleground keys (off by default)
        tbhost::autotrain::LoadConfig();       // TortoiseBots.AutoLearnOnLevel (default 1)
        TortoiseBots::questspells::LoadConfig();   // the class-quest spell switch, on by default
        TortoiseBots::talents::LoadConfig();       // automatic talent spending on login and level-up
        TortoiseBots::autoprofs::LoadConfig();
        TortoiseBots::autotools::LoadConfig();     // auto-grant missing profession tools to pool/free bots, on by default
        TortoiseBots::autohunterpet::LoadConfig(); // starter pet on level 10 / login for hunter bots without a pet
        TortoiseBots::autofood::LoadConfig();
        TortoiseBots::school::LoadConfig();        // #48: the spell-school switch and the gain it needs
        // A reload that turns the switch on builds the index at once (the stores are loaded by then); one that turns it off drops it.
        // BuildIndex reads the switch, so the load above must stay above this block.
        if (reload)
        {
            tbhost::autotrain::BuildIndex();
            TortoiseBots::questspells::BuildIndex();
            TortoiseBots::school::BuildIndex();
        }
        // A reload gives a failed engine config load (a missing aiplayerbot.conf, now present) another try at once; the first load is
        // made by OnWorldInitialized, when the stores the engine reads are there.
        if (reload && BotMgr::Instance().Enabled() && TortoiseBots::engine::Built())
            LoadEngineConfig();
        if (reload)
            TortoiseBots::cmd::LoadBindings();     // P2: the key or the graces changed, or persistence was just turned on
        TortoiseBots::cmd::LoadFeeds();            // P4: the map layers' keys and ranges (the boss roster is built by the world start, below)
        TortoiseBots::player::LoadConfig();        // R1: the `.bot` keys (TortoiseBots.Player.Commands, MaxBots)
        TortoiseBots::raidcons::LoadConfig();      // #22: the raid-consumable keys (off by default)
        TortoiseBots::resistgear::LoadConfig();    // #20: the resist-gear swap keys (off by default)
    }

    // `reload all` is the operator's "read the database again". The plan tables are the module's own (ai_dungeon_plan and
    // ai_dungeon_step_unit in the world DB): no core reload reads them, so this hook (WorldScript::OnAfterReload, ScriptMgr.h:295)
    // refreshes them through the same all-or-nothing path as `.tb plan reload`. The other core reloads do not touch anything the
    // module caches: the item, spell and talent mirrors come from the DB2 stores (never reloaded), the commander bindings from the
    // character DB (no core reload reads them) and the engine config through OnConfigLoad above.
    void OnAfterReload(std::string_view command) override
    {
        // The class-quest spell index holds Quest pointers into the store a quest reload replaces, so both reloads rebuild it, and a
        // disabled switch drops it (BuildIndex resets the index then): no stale pointer survives for a later re-enable to read.
        if (command == "reload all" || command.find("quest") != std::string_view::npos)
            TortoiseBots::questspells::BuildIndex();
        if (command == "reload all" || command.find("trainer") != std::string_view::npos)
            tbhost::autotrain::BuildIndex();
#ifdef TORTOISEBOTS_SLICE1
        // The quest starter/ender tables feed the bots' quest eligibility sets. A reload while the module is off still drops
        // them: the next check rebuilds, so a re-enable cannot read pre-reload rows.
        if (command == "reload all" || command.find("quest") != std::string_view::npos)
        {
            if (BotMgr::Instance().Enabled() && TortoiseBots::engine::Built())
                TortoiseBots::h3::RebuildQuestEligibility();
            else
                TortoiseBots::h3::ClearQuestEligibility();
        }
        if (command != "reload all" || !BotMgr::Instance().Enabled() || !ai::GetDungeonPlans().Sealed())
            return;
        // A run keeps the plan it started on and matches its units by (leg, seq): new unit rows under it could give it another step's
        // spawns. So while any run holds a plan the reload waits; `.tb plan reload` does it once the runs end.
        std::vector<std::string> const runs = TortoiseBots::run::PlanRunLeaders();
        if (!runs.empty())
        {
            std::string names;
            for (std::string const& leader : runs)
            {
                if (!names.empty())
                    names += ", ";
                names += leader;
            }
            TC_LOG_WARN("tortoisebots", "TortoiseBots: plans not reloaded: run(s) in progress ({}); use .tb plan reload after they end", names);
            return;
        }
        ai::PlanReloadHost host;
        TortoiseBots::run::FillUnitReloadHost(host);
        ai::PlanReloadReport const report = ai::ReloadDungeonPlans(TortoiseBots::run::PlansInUse(), &host);
        if (!report.outcome.swapped)
            TC_LOG_WARN("tortoisebots", "TortoiseBots: the dungeon plans kept after {} ({})", command,
                report.outcome.reason.empty() ? std::string("the reload was refused") : report.outcome.reason);
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots: reloaded {} dungeon plans ({} steps, {} step units) after {}", ai::GetDungeonPlans().PlanCount(),
                ai::GetDungeonPlans().StepCount(), TortoiseBots::run::LastStepUnitLoad().kept, command);
#else
        (void)command;
#endif
    }

    void OnWorldInitialized() override
    {
        BotMgr::Instance().LoadConfig();
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
        TortoiseBots::luabots::SetEnabled(BotMgr::Instance().Enabled());   // TBF-B1 finding 8 (see OnConfigLoad)
#endif
        TortoiseBots::Dashboard::LoadConfig();
        TortoiseBots::DashRuns::LoadConfig();
        TortoiseBots::pool::LoadConfig();      // W0
        TortoiseBots::pool::PoolScheduler::LoadConfig();   // W0-B
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        TortoiseBots::poolspawn::LoadConfig(); // the spawn kit key
#endif
        TortoiseBots::pool::Reconcile();       // W0: the creations that stopped halfway (a claim row, no account or character yet)
        // Startup only (the key is read here, never in OnConfigLoad): `.reload config` cannot start a reset.
        TortoiseBots::pool::Reset::PlanAtStartup(sConfigMgr->GetStringDefault("TortoiseBots.Pool.Reset", "off"));
        TortoiseBots::bg::LoadBgMapData();     // BG1: the flag stands and node banners from the core's spawn store (one pass)
        // The item store is loaded by now (World.cpp:1625, and item_classic_block at :1631); fill every mirror once so the
        // bot brain looks items up without a lock (TbItemPrototype.cpp).
        tbcompat::FillAll();
        TortoiseBots::Dashboard::WriteItemsFile();
        // The spell store is loaded too (World.cpp:1487). This hook is what tells the mirror so: it answers 0 and null
        // without touching the store until the flag is set, so a lookup from any earlier hook cannot index a half-loaded
        // store into a table that is never rebuilt.
        tbcompat::MarkSpellStoreReady();
        tbcompat::InitSpellTable();
        // The spell-school tables (#48): the names resolve in the spell mirror, which is ready just above.
        TortoiseBots::school::BuildIndex();
        // #22: the raid-consumable table, every row checked against the item and spell mirrors now that they answer.
        TortoiseBots::raidcons::LoadConfig();
        TortoiseBots::raidcons::BuildIndex();
        // #20: the boss -> school table, every row checked against its evidence spell's school now that the mirror answers.
        TortoiseBots::resistgear::BuildIndex();
        // #20: every stored resist-gear hold (a crash or a restart while a set was on), and a restore for a bot already in the world.
        TortoiseBots::resistgear::LoadAtStartup();
        // The class quests' spells, once, from the loaded quest store; a quest reload rebuilds it.
        tbhost::autotrain::LoadConfig();
        tbhost::autotrain::BuildIndex();
        if (TortoiseBots::questspells::Enabled())
            TortoiseBots::questspells::BuildIndex();
        TortoiseBots::talents::LogStartupSelfCheck();
        TC_LOG_INFO("server.loading", "TortoiseBots (ForeverCore port, spike): {}", BotMgr::Instance().Enabled() ? "enabled" : "disabled");
        // Without the slice nothing reaches an unported feature, so the lines are only for a build that has the engine.
#ifdef TORTOISEBOTS_SLICE1
        if (TortoiseBots::engine::Built())
        {
            TC_LOG_INFO("server.loading", "TortoiseBots: {}", tbcompat::UnportedBootLine());
            // The engine's own config (the 720-key read and the spell and talent tables) now, not on the first bot: it stalled the world
            // for 1.09 s at the first engine start (R8). Only when the module is on, as nothing builds an engine otherwise.
            if (BotMgr::Instance().Enabled())
            {
                LoadEngineConfig();
                TortoiseBots::hire::AdvertiseAddonCaps();   // TBM:CAPS after the engine config, not at script registration
                TortoiseBots::RunTraceService::Instance().Initialize();   // mark summaries the last server never finalised
                TortoiseBots::gear::InitGearIndex();
                TortoiseBots::gear::InitGearSourceIndex();
                TortoiseBots::enchant::LoadSpecEnchants();   // the level-60 enchant choice, one world table read
                TortoiseBots::hire::LoadAtStartup();   // P4: the contract gear sets (a missing table leaves tier sets off)
                TortoiseBots::run::LoadPlans();     // S5 (H5): the dungeon plan tables and the step unit rows
                TortoiseBots::cmd::LoadBindings();   // P2: TortoiseBots.Commander.KeepBindings and the stored rows
                TortoiseBots::cmd::LoadFeeds();      // P4: the boss roster (the plans are sealed now) and the feeds' keys
                tbhost::guild_orders::LoadOrders();  // W9: guarded characters-DB load of tortoise_bots_guild_order
                tbhost::guild_order_access::LoadBotRankAtStartup();  // the stored guild bot ranks
                tbhost::guild_orders::LoadDisenchantQueue(); // W9: the disenchant service queue
                TortoiseBots::h3::RebuildQuestEligibility();
            }
        }
#endif
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        // The add-on hint: the three keys once, then the characters already told (a guarded SELECT).
        TortoiseBots::addonhint::LoadConfig();
        TortoiseBots::addonhint::LoadAtStartup();
        TortoiseBots::poolspawn::LoadAtStartup();   // the spawn row's table, so a fresh bot can record its spawn
#endif
    }

    void OnBeforeMapsUpdate(uint32 diff) override
    {
        auto const started = std::chrono::steady_clock::now();
        BotMgr::Instance().Update(diff);
#ifdef TORTOISEBOTS_SLICE1
        // The hire ledger's grace, battleground boundary and one-per-tick deletion drain (host/hire/TbHireLifecycle.cpp).
        TortoiseBots::HireLifecycle::Instance().Update(diff);
#endif
        TortoiseBots::pool::Reset::Update(diff);   // the startup reset, one step and at most one deletion per tick
#ifdef TORTOISEBOTS_SLICE1
        TortoiseBots::PassingBuffsService::Instance().Update(diff);   // pool bots' buffs for the real players they pass
#endif
        bool const poolLogin = TortoiseBots::pool::PoolScheduler::Update(diff);   // W0-B: the pool's logins and logouts, once a second
        if (!poolLogin)
            TortoiseBots::pool::CreateUpdate(diff);   // W0: at most one pool character per rate tick, and never in a tick with a login
        TortoiseBots::bg::Update(diff);           // BG1: the queue fill, the invitations, the leaves and the spirit healer
        TortoiseBots::Dashboard::Update(diff);
        TortoiseBots::resistgear::Update(diff);   // #20: recover a stored hold, retry an unfinished restore (1 s sweep, backoff)
        uint32 const us = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        uint32 const worldMs = sWorldUpdateTime.GetLastUpdateTime();
        PerfWindow& perf = Perf();
        ++perf.ticks;
        perf.worldSumMs += worldMs;
        perf.worldMaxMs = std::max(perf.worldMaxMs, worldMs);
        perf.botSumUs += us;
        perf.botMaxUs = std::max(perf.botMaxUs, us);
        BotMgr::BudgetStats const budget = BotMgr::Instance().LastBudget();
        perf.poolBotsSum += budget.poolBots;
        perf.processedSum += budget.processed;
        perf.cutTicks += budget.cut ? 1 : 0;
        perf.heldTicks += budget.held ? 1 : 0;
        perf.lastBudgetUs = budget.budgetUs;
        perf.lastPoolUs = budget.poolUs;
        perf.lastCombatUs = budget.combatUs;
        perf.targetMs = budget.targetMs;
        perf.smoothedMs = budget.smoothedTickMs;
        if (BotMgr::Instance().Enabled())
        {
            TortoiseBots::perf::Window& botPerf = BotPerf();
            botPerf.elapsedMs += diff;
            ++botPerf.ticks;
            botPerf.passSumUs += us;
            botPerf.maxPassUs = std::max(botPerf.maxPassUs, us);
            botPerf.worldSumMs += worldMs;
            if (botPerf.elapsedMs >= 30000)
            {
                BotMgr::FleetCounts const counts = BotMgr::Instance().GetFleetCounts();
                TC_LOG_INFO("tortoisebots", "{}", TortoiseBots::perf::BotPerfLine(botPerf, counts.bots, counts.engines,
                    counts.owned, counts.mastered, counts.free, counts.pool));
                botPerf = TortoiseBots::perf::Window();
            }
        }
        else
            BotPerf() = TortoiseBots::perf::Window();
#ifdef TORTOISEBOTS_SLICE1
        TortoiseBots::run::Update(diff);      // S6 (C6): the dungeon runs, once a second each; its cost is its own (`.tb run status all`: tick_avg_us, tick_max_us), not in the share above
        TortoiseBots::hire::Update(diff);     // #298: the mustering companions (confirm, ledger, charge, login, provision, group)
        TortoiseBots::DashRuns::Update(diff);  // dashboard round 1: COMMANDERS and the run summaries (forever/docs/DASH-RUNS-R1-NOTES.md)
        TortoiseBots::cmd::Maintain();         // P2: the binding timers (dead lead, absent commander) and the stored rows whose party is back
        TortoiseBots::place::Update();          // #15: the minimap-ping timeouts and the walk back of a placed bot that lost its slot
        TortoiseBots::cmd::UpdateFeeds(diff);   // P4: the live position report (TortoiseBots.Commander.PosMs, 100 ms)
        TortoiseBots::cmd::combat::Update(diff);   // P5: the fight monitor's 1 Hz sample, 250 ms cast poll and COMBAT_FIGHT/EVENTS
        TortoiseBots::RunTraceService::Instance().Update(diff);   // run trace: the frame, the end poll, listener detach
        TortoiseBots::WhyTraceService::Instance().Update(diff);   // why trace: the timeout, a bot gone, strategy lines
        TortoiseBots::DecisionCaptureService::Instance().Update();   // decision capture: reap finished segment writers
#endif
    }

    void OnShutdown() override
    {
#ifdef TORTOISEBOTS_SLICE1
        TortoiseBots::RunTraceService::Instance().Shutdown();
        TortoiseBots::WhyTraceService::Instance().Shutdown();
        TortoiseBots::DecisionCaptureService::Instance().Shutdown();
#endif
        TortoiseBots::pool::Reset::Shutdown();
        // Save and remove every bot while the world is still up.
        BotMgr::Instance().LogoutAll("server shutdown");
        BotMgr::Instance().Update(0);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
        // TBF-B1 third review finding 8: only now, AFTER every bot is logged out and its last events are in the run table/flush buffer,
        // does the provider answer available() == false (the engine empties its snapshot; nothing of the shutdown is lost).
        TortoiseBots::luabots::Shutdown();
#endif
    }
};

// The dashboard's fight numbers: damage dealt and taken, healing and overhealing.
class tortoisebots_unit : public UnitScript
{
public:
    tortoisebots_unit() : UnitScript("tortoisebots_unit") { }
    void OnDamageSpell(Unit* attacker, Unit* victim, uint32 damage, SpellInfo const* spellInfo) override { TortoiseBots::Dashboard::OnDamage(attacker, victim, damage, spellInfo); }
    void OnHealOutcome(Unit* healer, Unit* /*target*/, uint32 /*spellId*/, uint32 raw, uint32 effective) override { TortoiseBots::Dashboard::OnHealOutcome(healer, raw, effective); }
    // P2: a dead lead bot starts its grace; the binding ends only after it (TortoiseBots.Slice.DeadBackstopSeconds).
    void OnUnitDeath(Unit* victim, Unit* /*killer*/) override
    {
        if (victim && victim->GetTypeId() == TYPEID_PLAYER)
            TortoiseBots::cmd::NoteDeath(victim->ToPlayer());
    }
};

// P2: the group events a commander binding ends on (the lead leaving, the group disbanding or losing its leader, another player joining). Called by the
// core's Group methods, the same ones BotMgr uses to form a bot party, so robot groups fire them too.
class tortoisebots_group : public GroupScript
{
public:
    tortoisebots_group() : GroupScript("tortoisebots_group") { }

    void OnAddMember(Group* group, ObjectGuid guid) override { TortoiseBots::cmd::NoteGroupAdd(group, guid); }
    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod /*method*/, ObjectGuid /*kicker*/, char const* /*reason*/) override
    {
#ifdef TORTOISEBOTS_SLICE1
        TortoiseBots::HireLifecycle::Instance().OnGroupMemberRemoved(group, guid);
#endif
        TortoiseBots::cmd::NoteGroupRemove(group, guid);
    }
    void OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid /*oldLeaderGuid*/) override { TortoiseBots::cmd::NoteGroupLeader(group, newLeaderGuid); }
    void OnDisband(Group* group) override
    {
#ifdef TORTOISEBOTS_SLICE1
        TortoiseBots::HireLifecycle::Instance().OnGroupDisband(group);
#endif
        TortoiseBots::cmd::NoteGroupDisband(group);
        TortoiseBots::h3::ForgetLayout(group);
    }
};

class tortoisebots_player : public PlayerScript
{
public:
    tortoisebots_player() : PlayerScript("tortoisebots_player") { }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        TortoiseBots::Dashboard::OnKill(killer, killed);
        if (killer && BotMgr::Instance().IsBot(killer->GetGUID()))
            BotMgr::Instance().NoteKill(killer->GetGUID());   // W1 `kills=`
    }
    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override { TortoiseBots::Dashboard::OnKilledByCreature(killer, killed); }
    void OnQuestStatusChange(Player* player, uint32 questId) override { TortoiseBots::Dashboard::OnQuestStatusChange(player, questId); }
    void OnLootItem(Player* player, Item* item) override { TortoiseBots::Dashboard::OnLootItem(player, item); }
    void OnMoneyChanged(Player* player, int64& amount) override { TortoiseBots::Dashboard::OnMoneyChanged(player, amount); }
    void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override { TortoiseBots::Dashboard::OnSpellCast(player, spell && !spell->IsTriggered() ? spell->GetSpellInfo() : nullptr); }

    // #15: a commander's whisper to one of its own bots is the same command `.tb cmd place|rally|fallback` takes.
    void OnChatSent(Player* player, uint32 type, uint32 /*lang*/, std::string const& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || !player || !receiver || BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        if (!BotMgr::Instance().IsBot(receiver->GetGUID()) || !TortoiseBots::cmd::IsCommandedBy(player, receiver))
            return;
        TortoiseBots::place::HandleChat(player, msg);
    }

    // #15: the same words in the party or raid chat of a group the commander drives.
    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Group* group) override
    {
        if (type != CHAT_MSG_PARTY && type != CHAT_MSG_PARTY_LEADER && type != CHAT_MSG_RAID && type != CHAT_MSG_RAID_LEADER)
            return;
        if (!player || BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        TortoiseBots::place::HandleChat(player, msg, group);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (BotMgr::Instance().IsBot(player->GetGUID()))
        {
            TC_LOG_INFO("tortoisebots", "bot {} entered the world on map {}", player->GetName(), player->GetMapId());
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
            // The start level before the level-bound spell, talent and profession syncs below.
            TortoiseBots::poolspawn::ApplyStartLevel(player);
            tbhost::prof::ClearBotErrands(player->GetGUID());
#endif
            // P11 (G8): a bot has no client to choose its ammo, so the core's pick prefers a stack that is drawn with a model (Player.h:1528).
            player->SetPreferDrawableAmmo(true);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
            TortoiseBots::p11::EnsureHunterStarterAmmo(player);
            if (!TortoiseBots::p11::EnsureHunterStarterWeapon(player))
                TC_LOG_INFO("tortoisebots", "TortoiseBots: hunter {} (level {}) has no ranged weapon; Auto Shot stays off until it gets a bow, gun or crossbow",
                    player->GetName(), uint32(player->GetLevel()));
            TortoiseBots::p11::EnsureShamanStarterTotems(player);
            // The /pvp preference of the flagged share of free bots: a streamless session cannot send CMSG_SET_PVP.
            tbhost::worldpvp::OnBotLogin(player);
            // A bot that went offline (or died) mid-fishing errand logs back in
            // with the pole in hand and would fight with a tool.
            tbhost::fishing::RepairPoleInHand(player);
#endif
            // #20: a resist set left on by a crash or a logout that could not put every piece back goes back first.
            TortoiseBots::resistgear::RecoverLogin(player);
            // A bot's first login after its creation, and every later one, catches up on the class-quest spells its level earns.
            tbhost::autotrain::Sync(player, "login");
            // Abilities the world grants through skill values (Dual Wield, Block,
            // Parry, the weapon "Shoot" spells): the core's own pass only runs when
            // a skill value is written, so a seeded bot can own the skill without
            // the spell and the core then refuses the off hand.
            tbhost::skillspells::Sync(player, "login");
            TortoiseBots::questspells::Sync(player, "login");
            TortoiseBots::talents::Sync(player, "login");
            TortoiseBots::autoprofs::Sync(player, "login");
            TortoiseBots::autotools::Sync(player, "login");
            TortoiseBots::autohunterpet::Sync(player, "login");
            TortoiseBots::autofood::Sync(player, "login");
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
            // The one-time kit, after the syncs have taught the level's spells (Dual Wield, weapon skills).
            TortoiseBots::poolspawn::SeedSpawnKit(player);
            // A kit bow or gun comes with no ammo, and the kit runs after the login ammo pass: match it here, not at the resync.
            TortoiseBots::p11::EnsureHunterStarterAmmo(player);
#endif
        }
        TortoiseBots::cmd::NoteLogin(player);     // P2: a commander back inside the grace, or the last member of a stored binding
#ifdef TORTOISEBOTS_SLICE1
        // A hire's master is back inside the disconnect grace: its companions rejoin the party (host/hire/TbHireLifecycle.cpp).
        TortoiseBots::HireLifecycle::Instance().OnMasterLogin(player);
#endif
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        TortoiseBots::addonhint::NoteLogin(player);   // a real player whose client has not spoken TBM yet
#endif
    }

    // A level gained any way (XP, `.character level`, `.tb level`) teaches the class-quest spells the new level earns.
    void OnLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (BotMgr::Instance().IsBot(player->GetGUID()))
        {
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
            // The spawn's start-level raise happens here, and OnLogin runs the same syncs right after: skip this pass.
            if (TortoiseBots::poolspawn::ApplyingStartLevel())
                return;
#endif
            tbhost::autotrain::Sync(player, "level");
            tbhost::skillspells::Sync(player, "level");
            TortoiseBots::questspells::Sync(player, "level");
            TortoiseBots::talents::Sync(player, "level");
            TortoiseBots::autoprofs::Sync(player, "level");
            TortoiseBots::autotools::Sync(player, "level");
            TortoiseBots::autohunterpet::Sync(player, "level");
            TortoiseBots::autofood::Sync(player, "level");
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
            TortoiseBots::luabots::Invalidate(player->GetGUID());   // TBF-B1: the level's talents change the cached spec/role
#endif
        }
    }

    // TBF-B1: the spec/role words the Lua bot snapshot caches change with the talents too (host/TbLuaBots.cpp).
    void OnFreeTalentPointsChanged(Player* player, uint32 /*points*/) override
    {
        if (!player || !BotMgr::Instance().IsBot(player->GetGUID()))
            return;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
        TortoiseBots::luabots::Invalidate(player->GetGUID());
#endif
    }

    void OnTalentsReset(Player* player, bool /*involuntarily*/) override
    {
        if (!player || !BotMgr::Instance().IsBot(player->GetGUID()))
            return;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
        TortoiseBots::luabots::Invalidate(player->GetGUID());
#endif
    }

    void OnLogout(Player* player) override
    {
        if (BotMgr::Instance().IsBot(player->GetGUID()))
            TC_LOG_INFO("tortoisebots", "bot {} left the world", player->GetName());
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        TortoiseBots::poolspawn::Forget(player->GetGUID());
#endif
        TortoiseBots::cmd::NoteLogout(player);    // P2: a lead's departure, or the commander's grace (a shutdown keeps the binding)
        TortoiseBots::place::NoteLogout(player);  // #15: a commander's rallies, holds and pending cursor request go with it
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        tbhost::ritualfill::NoteLogout(player);   // #14: a bot's ritual-fill hold (and the targets it started) goes with it
        TortoiseBots::addonhint::Forget(player->GetGUID());   // a login is one session: nothing pends across it
#endif
        // #20: the pieces the bot's own are due back before the Player is deleted (the hold map is host state: it must not leak).
        TortoiseBots::resistgear::Restore(player, "logout");
#ifdef TORTOISEBOTS_SLICE1
        // A hire's master logs out: the disconnect grace starts, and the companions wait (host/hire/TbHireLifecycle.cpp).
        TortoiseBots::HireLifecycle::Instance().OnMasterLogout(player);
#endif
        // Before the Player is deleted: the engine brain (PlayerbotAI) must be gone by then.
        BotMgr::Instance().OnPlayerLogout(player);
    }
};

// #15: the commander's minimap ping (CMSG_MINIMAP_PING) completes a pending `place ... cursor`. The packet is only read: its handler still runs.
class tortoisebots_server : public ServerScript
{
public:
    tortoisebots_server() : ServerScript("tortoisebots_server") { }
    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        TortoiseBots::place::OnPacket(session, packet);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        // #14: a real player's use of a multi-user object; the packet is only read here (host/TbHostRitualFill.h).
        tbhost::ritualfill::OnPacket(session, packet);
#endif
    }
};

class tortoisebots_commands : public CommandScript
{
public:
    tortoisebots_commands() : CommandScript("tortoisebots_commands") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        // `create` makes accounts and `prune confirm` deletes them (and their characters): the permissions the core asks for
        // `.account create` and `.account delete`, not the lowest GM one. The dry run of `prune` stays at the GM level.
        static ChatCommandTable poolTable =
        {
            { "create", HandlePoolCreate, rbac::RBAC_PERM_COMMAND_ACCOUNT_CREATE, Console::Yes },
            { "prune",  HandlePoolPrune,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "start",  HandlePoolStart,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // W0-B: the scheduler
            { "stop",   HandlePoolStop,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "status", HandlePoolStatus, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "population", HandlePoolPopulation, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // F-population: the online target at runtime
        };
        static ChatCommandTable tbTable =
        {
            { "create", HandleCreate, rbac::RBAC_PERM_COMMAND_ACCOUNT_CREATE, Console::Yes },   // W0: makes characters, so it asks what `.tb pool create` asks
            { "login",  HandleLogin,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "group",  HandleGroup,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "ungroup", HandleUngroup, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "logout", HandleLogout, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "cast",   HandleCast,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "go",     HandleGo,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "hop",    HandleHop,    rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "raid",   HandleRaid,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "enter",  HandleEnter,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // H4: `.tb enter <bot|party> ...` is TortoiseBots::h4::HandleEnter (below)
            { "attack", HandleAttack, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "lootnear", HandleLootNear, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "where",  HandleWhere,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "engine", HandleEngine, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "status", HandleStatus, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "rage",   HandleRage,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "perf",   HandlePerf,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "spells", HandleSpells, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "unported", HandleUnported, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "item",   HandleItem,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "spellscan", HandleSpellScan, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "spell",  HandleSpell,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "ranks",  HandleRanks,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "castfail", HandleCastFail, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "level",  HandleLevel,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "train",  HandleTrain,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "mount",  HandleMount,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "hurt",   HandleHurt,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "power",  HandlePower,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "additem", HandleAddItem, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "removeitem", TortoiseBots::h3::HandleRemoveItem, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "use",    HandleUse,    rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "castlog", HandleCastLog, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "order",  TortoiseBots::h3::HandleOrder,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "role",   TortoiseBots::h3::HandleRole,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "lfg",    TortoiseBots::lfg::HandleLfg,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "master", TortoiseBots::h3::HandleMaster, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "party",  TortoiseBots::h3::HandleParty,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "layout", TortoiseBots::h3::HandleLayout, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "onyxia", TortoiseBots::onyxia::HandleCommand, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "state",  TortoiseBots::h3::HandleState,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "threat", TortoiseBots::h3::HandleThreat, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "school", TortoiseBots::h3::HandleSchool, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // #48: the school switch's read-out (host/TbSchool.h)
            { "learn",  TortoiseBots::h3::HandleLearn,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "equip",  TortoiseBots::h3::HandleEquip,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "itemscan", TortoiseBots::h3::HandleItemScan, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "statcensus", TortoiseBots::h3::HandleStatCensus, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "grammar", TortoiseBots::h3::HandleGrammar, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "trigger", TortoiseBots::h4::HandleTrigger, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "instance", TortoiseBots::h4::HandleInstance, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "tele",   TortoiseBots::h4::HandleTele,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "revive", TortoiseBots::h4::HandleRevive, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "kill",   TortoiseBots::h4::HandleKill,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "gm",     TortoiseBots::h4::HandleGm,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "plan",   TortoiseBots::run::HandlePlan,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "run",    TortoiseBots::run::HandleRun,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "trace",  TortoiseBots::trace::HandleTrace, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // run trace (runtime/RunTrace.cpp)
            { "why",    TortoiseBots::trace::HandleWhy,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },     // one bot's AI decisions (runtime/WhyTrace.cpp)
            { "cc",       TortoiseBots::h8::HandleCc,       rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // H8: crowd-control mark of one bot
            { "strategy", TortoiseBots::h8::HandleStrategy, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // H8: `.tb strategy <bot|role:...> co|nc +name,-name,~name | <bot> reset` (SETUP)
            { "pet",      TortoiseBots::p11::HandlePet,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // P11: `.tb pet <bot> give|call|dismiss|show|order ...` (host/TbPet.cpp)
            { "commander", TortoiseBots::cmd::HandleCommander, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // P1: `.tb commander status [<commander>]`
            { "addonhint", HandleAddonHint, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // `.tb addonhint status|reset <character>`
            { "pool",   poolTable },   // W0: `.tb pool create <n>`, `.tb pool prune [confirm]`
            { "bg",     HandleBg,     rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // BG1: `.tb bg status|events` (host/bg/TbBgQueue.h)
            { "gear",   TortoiseBots::h3::HandleGear,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "cmd",      TortoiseBots::cmd::HandleCmd,       rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "quest",     TortoiseBots::h3::HandleQuest,      rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "ignore", TortoiseBots::i13::HandleIgnore, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "los",    TortoiseBots::losq::HandleLos,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // #308: `.tb los <a> <b> [map=<id>]` (host/TbLosQuery.cpp)
            { "free",   HandleFree,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "link",   TortoiseBots::chatlink::HandleLink, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // S1-R1: `.tb link <text> [#token]` (host/TbChatLinkCmd.cpp, both builds)
            { "hear",   TortoiseBots::chatin::HandleHear, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },     // S1-R2: `.tb hear <bot> <sender> whisper|party <text> [#token]` (host/chat/TbChatCmd.cpp, engine build)
            { "pushback", HandlePushback, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "knock",  TortoiseBots::kb::HandleKnock, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // S3: `.tb knock <bot> <speedXY> <speedZ>`
            { "speed",  TortoiseBots::kb::HandleSpeed, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },   // S3: `.tb speed <bot> run|walk|swim <rate>`
            { "setskill", HandleSetSkill, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "guildorder", TortoiseBots::h3::HandleGuildOrder, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "craftorder", TortoiseBots::h3::HandleCraftOrder, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "spec",   TortoiseBots::h3::HandleSpec,   rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "talents", TortoiseBots::talents::HandleTalentsCommand, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "blessing", TortoiseBots::blessing::HandleBlessingChat, rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "lootlog", TortoiseBots::lootlog::HandleLootLogChat, rbac::RBAC_PERM_COMMAND_GM, Console::No },
            { "ai",       TortoiseBots::aipreset::HandleAiCommand, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
            { "cs",       TortoiseBots::aipreset::HandleCsCommand, rbac::RBAC_PERM_COMMAND_GM, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "tb", tbTable },
        };
        return commandTable;
    }

    static Player* Caller(ChatHandler* handler)
    {
        return handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    }

    static bool Fail(ChatHandler* handler, std::string const& text)
    {
        handler->PSendSysMessage("TortoiseBots: %s", text.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    static bool RequireOwnedBot(ChatHandler* handler, ObjectGuid botGuid)
    {
        if (botGuid.IsEmpty() || !BotMgr::Instance().IsBot(botGuid))
            return true;
        std::string code, detail;
        if (!TortoiseBots::h3::CheckBotAccess(handler, botGuid, code, detail))
            return Fail(handler, detail + " (" + code + ").");
        return true;
    }

    // .tb addonhint status|reset <character>: the add-on hint's stored row (host/TbHostAddonHint.cpp).
    static bool HandleAddonHint(ChatHandler* handler, Trinity::ChatCommands::Tail args)
    {
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        return TortoiseBots::addonhint::HandleCommand(handler, args);
#else
        handler->SendSysMessage("TortoiseBots: addonhint: the engine is not built in (this build has no add-on channel).");
        handler->SetSentErrorMessage(true);
        return false;
#endif
    }

    // .tb create <account> <name> <race id> <class id> [gender 0|1]: a new level 1 character for a bot.
    static bool HandleCreate(ChatHandler* handler, std::string account, std::string name, uint8 race, uint8 playerClass, Optional<uint8> gender)
    {
        if (!normalizePlayerName(name))
            return Fail(handler, "that name is not allowed.");
        // Only a pool account or a test account the config names (TortoiseBots.Create.TestAccounts) takes a character from here;
        // a real player's account never does.
        if (!TortoiseBots::world::CreateAccountAllowed(account, sConfigMgr->GetStringDefault("TortoiseBots.Create.TestAccounts", "")))
            return Fail(handler, account + " is not a pool account (TBPOOL<nnnn>) or a test account named in TortoiseBots.Create.TestAccounts: no character is made on it.");
        std::string const error = BotMgr::Instance().CreateCharacter(AccountMgr::GetId(account), name, race, playerClass, gender.value_or(0));
        if (!error.empty())
            return Fail(handler, name + " not created: " + error + ".");
        // The saves are queued; the character cache learns of the character once the characters database has it.
        handler->PSendSysMessage("TortoiseBots: %s created (its saves are queued; it is listed once the database has it).", name.c_str());
        return true;
    }

    // .tb login <character> [master|free]: the character joins the world as a bot. It follows <master>, or the caller when
    // no master is named and the command comes from a player. `free` = a masterless bot that grinds, loots and corpse-runs on
    // its own (W1). From the console without a master it stands still.
    static bool HandleLogin(ChatHandler* handler, std::string name, Optional<std::string> masterName, Optional<std::string> extra)
    {
        ObjectGuid master;
        bool free = false;
        std::vector<std::string> words;
        if (masterName)
            words.push_back(*masterName);
        if (extra)
            words.push_back(*extra);
        for (std::string const& word : words)
        {
            if (Lower(word) == "free")
            {
                if (free)
                    return Fail(handler, "`free` is named twice.");
                free = true;
                continue;
            }
            if (!master.IsEmpty())
                return Fail(handler, "name one master (or `free`), not two.");
            master = GuidByName(word);
            if (master.IsEmpty())
                return Fail(handler, "no character named " + word + ".");
            if (!TortoiseBots::h3::MayLeadBots(handler, master))   // N5
                return Fail(handler, word + " is neither a host bot nor your own character: it cannot be the master of a bot.");
        }
        if (free && !master.IsEmpty())
            return Fail(handler, "a free bot has no master: name `free` or a master, not both.");
        if (!free && master.IsEmpty())
            if (Player* caller = Caller(handler))
                master = caller->GetGUID();

        ObjectGuid const guid = GuidByName(name);
        std::string const error = BotMgr::Instance().Login(guid, master, free, false, false, false, true);
        if (!error.empty())
            return Fail(handler, name + " not logged in: " + error + ".");
        // A bot with no master that takes the engine (EngineByDefault, `owned`) runs the free kit, so say so.
        bool const freeBot = free || BotMgr::Instance().IsFreeBot(guid);
        handler->PSendSysMessage("TortoiseBots: %s is logging in%s.", name.c_str(), freeBot ? " as a free bot" : "");
        return true;
    }

    // `.tb pool prune [confirm]` (W0): the failed rows of stopped creations, listed (a dry run) unless `confirm` re-verifies and
    // removes them. A dry run needs the GM permission of the table; `confirm` deletes accounts and their characters, so it needs
    // the permission of `.account delete` (the console has every permission). The scheduler's words (start, stop, status) are
    // round W0-B's.
    static bool HandlePoolPrune(ChatHandler* handler, Optional<std::string> argument)
    {
        bool confirm = false;
        if (argument)
        {
            if (Lower(*argument) != "confirm")
                return Fail(handler, "usage: `.tb pool prune` (a dry run) or `.tb pool prune confirm`.");
            if (!handler->HasPermission(rbac::RBAC_PERM_COMMAND_ACCOUNT_DELETE))
                return Fail(handler, "`.tb pool prune confirm` deletes accounts: it needs the permission of `.account delete`. `.tb pool prune` alone is the dry run.");
            confirm = true;
        }
        TortoiseBots::pool::PruneReport const report = TortoiseBots::pool::Prune(confirm);
        std::size_t const shown = report.lines.size() < 20 ? report.lines.size() : std::size_t(20);
        for (std::size_t i = 0; i < shown; ++i)
            handler->PSendSysMessage("TortoiseBots: %s", report.lines[i].c_str());
        if (report.lines.size() > shown)
            handler->PSendSysMessage("TortoiseBots: ... and %u more line(s).", uint32(report.lines.size() - shown));
        if (!report.enabled)
        {
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (!report.applied)
            handler->PSendSysMessage("TortoiseBots: prune dry run: %u account(s), %u character(s) and %u row(s) would be removed; run `.tb pool prune confirm` to remove them.",
                report.accounts, report.characters, report.rows);
        return true;
    }

    // `.tb pool create <n>` (W0): n level-1 pool characters, one at a time at TortoiseBots.Pool.CreatePerMinute per minute. It makes
    // accounts, so the table row asks the permission of `.account create`.
    static bool HandlePoolCreate(ChatHandler* handler, std::string argument)
    {
        std::uint32_t count = 0;
        for (char const c : argument)
        {
            if (c < '0' || c > '9' || count > 99999)
                return Fail(handler, "usage: `.tb pool create <n>`.");
            count = count * 10 + std::uint32_t(c - '0');
        }
        if (!count)
            return Fail(handler, "usage: `.tb pool create <n>`.");
        TortoiseBots::pool::CreateStart const start = TortoiseBots::pool::StartCreate(count);
        if (!start.started)
            return Fail(handler, start.message);
        handler->PSendSysMessage("TortoiseBots: %s.", start.message.c_str());
        return true;
    }

    // `.tb pool start|stop|status` (W0-B): the scheduler of host/pool/TbPoolScheduler.h. Start is explicit; stop drains every
    // pool bot at 5 logouts per second; status is one line for the console and the live checks.
    static bool HandlePoolStart(ChatHandler* handler)
    {
        TortoiseBots::pool::PoolScheduler::SwitchResult const result = TortoiseBots::pool::PoolScheduler::Start();
        handler->PSendSysMessage("TortoiseBots: %s", result.message.c_str());
        if (!result.ok)
            handler->SetSentErrorMessage(true);
        return result.ok;
    }

    static bool HandlePoolStop(ChatHandler* handler)
    {
        TortoiseBots::pool::PoolScheduler::SwitchResult const result = TortoiseBots::pool::PoolScheduler::Stop();
        handler->PSendSysMessage("TortoiseBots: %s", result.message.c_str());
        if (!result.ok)
            handler->SetSentErrorMessage(true);
        return result.ok;
    }

    static bool HandlePoolStatus(ChatHandler* handler)
    {
        for (std::string const& line : TortoiseBots::pool::PoolScheduler::Status())
            handler->PSendSysMessage("TortoiseBots: %s", line.c_str());
        handler->PSendSysMessage("TortoiseBots: pool %s", TortoiseBots::pool::Reset::StatusLine().c_str());
        return true;
    }

    // `.tb bg status` | `.tb bg events [since=<seq>] [#token]` (BG1, host/bg/TbBgQueue.h): one `bg ...` record per line and one
    // `bg-end` line, so a client can tell an empty answer from a broken one.
    static bool HandleBg(ChatHandler* handler, Tail args)
    {
        std::vector<std::string> words = WordsOf(args);
        std::string token;
        if (!words.empty() && words.back().size() > 1 && words.back()[0] == '#')
        {
            token = words.back();
            words.pop_back();
        }
        static char const* const usage = "usage: `.tb bg status` or `.tb bg events [since=<seq>]`.";
        if (words.empty())
            return Fail(handler, usage);
        std::string const verb = Lower(words[0]);
        if (verb == "status")
        {
            if (words.size() != 1)
                return Fail(handler, usage);
            std::vector<std::string> const lines = TortoiseBots::bg::Status();
            for (std::string const& line : lines)
                handler->PSendSysMessage("TortoiseBots: %s", line.c_str());
            handler->PSendSysMessage("TortoiseBots: bg-end verb=status lines=%zu %s", lines.size(), token.c_str());
            return true;
        }
        if (verb == "events")
        {
            std::uint32_t since = 0;
            bool haveSince = false;
            for (size_t i = 1; i < words.size(); ++i)
            {
                std::string const word = Lower(words[i]);
                if (word.compare(0, 6, "since=") != 0 || haveSince || word.size() == 6)
                    return Fail(handler, usage);
                char* end = nullptr;
                unsigned long const value = std::strtoul(word.c_str() + 6, &end, 10);
                if (!end || *end)
                    return Fail(handler, usage);
                since = std::uint32_t(value);
                haveSince = true;
            }
            TortoiseBots::bg::EventPage const page = TortoiseBots::bg::Events(since);
            for (std::string const& line : page.lines)
                handler->PSendSysMessage("TortoiseBots: %s", line.c_str());
            handler->PSendSysMessage("TortoiseBots: bg-end verb=events since=%u count=%zu oldest=%u last=%u %s", since,
                page.lines.size(), page.oldest, page.last, token.c_str());
            return true;
        }
        return Fail(handler, usage);
    }

    // `.tb pool population <n|low|high>` (F-population): the pool's online target at runtime. `low` and `high` are the
    // TortoiseBots.Pool.PopulationLow/High presets; a number is used as-is. The pool adds or logs out bots gradually from the
    // next pass (logins at LoginsPerMinute, logouts at 5 per second, never a dead or fighting bot); `.reload config` puts the
    // configured TargetOnline back.
    static bool HandlePoolPopulation(ChatHandler* handler, std::string word)
    {
        TortoiseBots::pool::PoolScheduler::SwitchResult const result = TortoiseBots::pool::PoolScheduler::SetPopulation(Lower(word));
        handler->PSendSysMessage("TortoiseBots: %s", result.message.c_str());
        if (!result.ok)
            handler->SetSentErrorMessage(true);
        return result.ok;
    }

    // .tb free <bot> on|off: a logged-in bot without a master plays the world on its own (W1). Off puts it back to standing.
    static bool HandleFree(ChatHandler* handler, std::string name, std::string state)
    {
        std::string const word = Lower(state);
        if (word != "on" && word != "off")
            return Fail(handler, "usage: `.tb free <bot> on|off`.");
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().SetFree(guid, word == "on");
        if (!error.empty())
            return Fail(handler, name + " is not free: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s free is %s.", name.c_str(), word.c_str());
        return true;
    }

    static std::string Lower(std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // .tb group <bot> <master>: two logged-in characters: the bot joins the master's party (the master leads) and takes it as its master.
    static bool HandleGroup(ChatHandler* handler, std::string name, std::string masterName)
    {
        ObjectGuid const master = GuidByName(masterName);
        if (master.IsEmpty())
            return Fail(handler, "no character named " + masterName + ".");
        if (!TortoiseBots::h3::MayLeadBots(handler, master))   // N5
            return Fail(handler, masterName + " is neither a host bot nor your own character: bots do not join a real player's party.");
        if (BotMgr::Instance().IsBot(master) && !RequireOwnedBot(handler, master))
            return false;
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().GroupWith(guid, master, true);
        if (!error.empty())
            return Fail(handler, name + " not grouped: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s joined the party of %s.", name.c_str(), masterName.c_str());
        return true;
    }

    // .tb ungroup <bot>: the bot leaves its party and has no master afterwards.
    static bool HandleUngroup(ChatHandler* handler, std::string name)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().Ungroup(guid);
        if (!error.empty())
            return Fail(handler, name + " not ungrouped: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s left its party.", name.c_str());
        return true;
    }

    // .tb go <character> <dx> <dy>: the bot walks that many yards east/north of where it stands (no master only).
    static bool HandleGo(ChatHandler* handler, std::string name, float dx, float dy)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        if (!BotMgr::Instance().WalkBy(guid, dx, dy))
            return Fail(handler, name + " is not a logged-in bot.");
        handler->PSendSysMessage("TortoiseBots: %s walks.", name.c_str());
        return true;
    }

    // .tb hop <character> <dx> <dy>: the bot is teleported that many yards east/north on its own map (R10: the engine's teleport handler, `.tb where`).
    static bool HandleHop(ChatHandler* handler, std::string name, float dx, float dy)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().Hop(guid, dx, dy);
        if (!error.empty())
            return Fail(handler, name + " not teleported: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s is teleported.", name.c_str());
        return true;
    }

    // .tb raid <master>: converts the master's party into a raid (R10 test helper for the "no bot joins a raid" rule).
    static bool HandleRaid(ChatHandler* handler, std::string name)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().ConvertToRaid(guid);
        if (!error.empty())
            return Fail(handler, name + "'s party not converted: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: the party of %s is a raid now.", name.c_str());
        return true;
    }

    // .tb enter <character> <mapid>: may that character enter that map, according to the core (R11 test helper, `.tb where` shows the same reason for a blocked follower).
    // H4 (slice 4): `.tb enter <bot|party [leader]> [trigger <id> | map <id>] [direct] [#token]` makes the bots enter a dungeon through its area trigger
    // (host/TbH4.cpp). The old two-word form (a character and a map NUMBER) is kept as it was; every other shape is the new command.
    static bool HandleEnter(ChatHandler* handler, Tail args)
    {
        std::vector<std::string> const words = WordsOf(args);
        bool legacy = words.size() == 2 && !words[1].empty();
        if (legacy)
        {
            std::string first = words[0];
            std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            legacy = first != "party";
            for (char c : words[1])
                legacy = legacy && std::isdigit(static_cast<unsigned char>(c));
        }
        if (!legacy)
            return TortoiseBots::h4::HandleEnter(handler, args);
        uint32 const mapId = uint32(std::strtoul(words[1].c_str(), nullptr, 10));
        for (std::string const& line : BotMgr::Instance().CanEnter(GuidByName(words[0]), mapId))
            handler->PSendSysMessage("TortoiseBots: %s: %s", words[0].c_str(), line.c_str());
        return true;
    }

    // .tb attack <character>: the bot attacks the nearest thing it may attack within 40 yards.
    static bool HandleAttack(ChatHandler* handler, std::string name)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const result = BotMgr::Instance().AttackNearest(guid, 40.0f);
        handler->PSendSysMessage("TortoiseBots: %s: %s.", name.c_str(), result.c_str());
        return true;
    }

    // .tb lootnear <bot>: test helper; the bot takes every item it may take from the dead creatures within 10 yd through the
    // core's own Player::StoreLootItem (what a client's loot-item request runs), so the item-loot path and its hooks run without
    // the engine (whose item looting is not ported yet).
    static bool HandleLootNear(ChatHandler* handler, std::string name)
    {
        Player* bot = RequireBot(handler, name);
        if (!bot || !bot->FindMap() || bot->IsBeingTeleported())
            return false;
        FindCreatureOptions options;
        options.IsAlive = FindCreatureAliveState::Dead;
        std::list<Creature*> corpses;
        bot->GetCreatureListWithOptionsInGrid(corpses, 10.0f, options);
        uint32 stored = 0, seen = 0;
        for (Creature* corpse : corpses)
        {
            Loot* loot = corpse->GetLootForPlayer(bot);
            if (!loot)
                continue;
            ++seen;
            for (uint8 slot = 0; slot < loot->items.size(); ++slot)
            {
                uint8 const before = loot->unlootedCount;
                bot->StoreLootItem(corpse->GetGUID(), slot, loot);
                if (loot->unlootedCount < before)
                    ++stored;
            }
        }
        handler->PSendSysMessage("TortoiseBots: %s: %u corpse(s) with loot within 10 yd, %u item(s) stored.", name.c_str(), seen, stored);
        return true;
    }

    // .tb where <character>: where the bot is and what it is doing.
    static bool HandleWhere(ChatHandler* handler, std::string name)
    {
        Player* bot = RequireBot(handler, name, false);
        if (!bot)
            return false;
        Unit* victim = bot->GetVictim();
        handler->PSendSysMessage("TortoiseBots: %s level %u map %u at %.1f %.1f %.1f, health %u/%u, %s, victim %s (health %u).",
            name.c_str(), uint32(bot->GetLevel()), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
            uint32(bot->GetHealth()), uint32(bot->GetMaxHealth()), bot->IsInCombat() ? "in combat" : "out of combat",
            victim ? victim->GetName().c_str() : "none", victim ? uint32(victim->GetHealth()) : 0u);
        {
            uint64 const money = bot->GetMoney();
            handler->PSendSysMessage("TortoiseBots: loot: UNIT_FLAG_LOOTING %s, %u window(s) in the area loot view.", bot->HasUnitFlag(UNIT_FLAG_LOOTING) ? "set" : "clear",
                uint32(bot->GetAELootView().size()));
            handler->PSendSysMessage("TortoiseBots: money: %llu copper (%llug %llus %lluc), mana %d/%d.", (unsigned long long)money,
                (unsigned long long)(money / 10000), (unsigned long long)((money / 100) % 100), (unsigned long long)(money % 100),
                bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA));
        }
        for (std::string const& line : BotMgr::Instance().DescribeBrain(bot->GetGUID()))
            handler->PSendSysMessage("TortoiseBots: %s", line.c_str());
#ifdef TORTOISEBOTS_SLICE1
        // R6 live check: the liquid at the bot's feet and the nearest wet floor cells within 40 yd (the run's own liquid rule, TbRunLiquid.h).
        // Review fix 3b: FindMap, not GetMap (which ASSERTs while the bot is between maps); no map, no liquid lines.
        if (Map* map = bot->FindMap())
            for (std::string const& line : TortoiseBots::run::liquid::DescribeAround(map, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 40.0f, 3.0f, 12))
                handler->PSendSysMessage("TortoiseBots: %s", line.c_str());
#endif

        // What the bot has around it: proves its part of the map is loaded and populated.
        std::list<Creature*> creatures;
        bot->GetCreatureListWithEntryInGrid(creatures, 0, 60.0f);
        Creature* nearest = nullptr;
        uint32 corpses = 0;
        for (Creature* creature : creatures)
        {
            if (!creature->IsAlive())
                ++corpses;
            if (creature->IsAlive() && (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest)))
                nearest = creature;
        }
        handler->PSendSysMessage("TortoiseBots: corpses within 60 yd: %u.", corpses);
        if (nearest)
        {
            handler->PSendSysMessage("TortoiseBots: %u creature(s) within 60 yd, nearest alive %s at %.1f yd (may attack: %s, friendly: %s).",
                uint32(creatures.size()), nearest->GetName().c_str(), bot->GetDistance(nearest),
                bot->IsValidAttackTarget(nearest) ? "yes" : "no", bot->IsFriendlyTo(nearest) ? "yes" : "no");
            handler->PSendSysMessage("TortoiseBots: sees it: %s, bot unit flags 0x%X player flags 0x%X, target unit flags 0x%X, target faction %u, uninteractible: %s.",
                bot->CanSeeOrDetect(nearest) ? "yes" : "no", uint32(*bot->m_unitData->Flags), uint32(*bot->m_playerData->PlayerFlags),
                uint32(*nearest->m_unitData->Flags), nearest->GetFaction(), nearest->IsUninteractible() ? "yes" : "no");
        }
        else
            handler->PSendSysMessage("TortoiseBots: %u creature(s) within 60 yd, none alive.", uint32(creatures.size()));
        return true;
    }

    // .tb rage <character> <rage points>: sets the bot's rage (the core stores rage times ten), the console's stand-in for `.modify rage`
    // (which needs a selected target): a level 1 warrior's short fights never reach the 40 rage the heroic strike trigger asks for.
    static bool HandleRage(ChatHandler* handler, std::string name, uint32 points)
    {
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        bot->SetPower(POWER_RAGE, int32(std::min<uint32>(points, 100) * 10));
        handler->PSendSysMessage("TortoiseBots: %s rage is %d/%d (core units).", name.c_str(), bot->GetPower(POWER_RAGE), bot->GetMaxPower(POWER_RAGE));
        return true;
    }

    // .tb castfail <character> <spell id> <reason> [interrupt]: sends the bot a real SMSG_CAST_FAILED (with `interrupt`: a
    // SMSG_SPELL_FAILURE of its own cast) so the cast-result path can be tested without a fight: session reader, bounded queue,
    // drain at the next engine tick (SLICE2-R0-NOTES.md). `reason` is the core's SpellCastResult number (SharedDefines.h:
    // 63 interrupted, 75 moving, 87 not known, 118 out of range, 130 targets dead).
    static bool HandleCastFail(ChatHandler* handler, std::string name, uint32 spellId, uint32 reason, Optional<std::string> mode)
    {
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        bool const interrupted = mode && *mode == "interrupt";
        if (mode && !interrupted)
            return Fail(handler, "the optional last word is `interrupt` (an SMSG_SPELL_FAILURE); without it an SMSG_CAST_FAILED is sent.");
        std::string const error = TortoiseBots::engine::InjectCastResult(bot, spellId, reason, interrupted);
        if (!error.empty())
            return Fail(handler, name + ": no result sent: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s: sent %s spell %u reason %u; the engine hears it on its next tick (`.tb where` shows the counts).",
            name.c_str(), interrupted ? "SMSG_SPELL_FAILURE" : "SMSG_CAST_FAILED", spellId, reason);
        return true;
    }

    // .tb pushback <character> <ms>: sends the bot a real SMSG_SPELL_DELAYED of its own cast (the packet Spell::Delayed sends to the set when
    // a hit pushes the bar back), so the reader, the queue and the engine's wait can be tested without a mob: the engine adds the delay to its
    // own wait on the next tick and writes SPELL_DELAYED (SLICE-SD notes). 0 and values above 1000 add nothing, as the legacy handler had it.
    static bool HandlePushback(ChatHandler* handler, std::string name, uint32 delayMs)
    {
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        std::string const error = TortoiseBots::engine::InjectSpellDelay(bot, delayMs);
        if (!error.empty())
            return Fail(handler, name + ": no pushback sent: " + error + ".");
        uint32 const extra = TortoiseBots::sd::PushbackExtraMs(int64(delayMs));
        handler->PSendSysMessage("TortoiseBots: %s: sent SMSG_SPELL_DELAYED %u ms; the engine adds %u ms to its wait (`.tb state` shows delayed=<n>).",
            name.c_str(), delayMs, extra);
        return true;
    }


    // ---- Slice 2 test commands (FOREVER-SLICE2-SPEC section 7, R3). They exist for the live test of the engine's priest, so each refuses
    // in a build without the engine (the option TORTOISEBOTS_SLICE1 is off). `<name>` is a character that is logged in through `.tb login`.
    static bool RequireEngine(ChatHandler* handler)
    {
        if (TortoiseBots::engine::Built())
            return true;
        return Fail(handler, "the engine is not built in (this build has no test commands for it).");
    }

    // The character of a module bot or of a master that is a bot session (logged in with `.tb login`), or a clear refusal. The commands that CHANGE a
    // character (hurt, power, level, train, additem, use, rage, cast, castlog sink...) go through here: a real player's character is refused with a message
    // (a GM has the core's own commands for that), R5 item 6.
    static Player* RequireBot(ChatHandler* handler, std::string const& name, bool requireControl = true)
    {
        ObjectGuid const guid = GuidByName(name);
        if (Player* bot = BotMgr::Instance().FindBot(guid))
        {
            if (requireControl && !RequireOwnedBot(handler, guid))
                return nullptr;
            return bot;
        }
        if (!guid.IsEmpty() && BotMgr::Instance().IsBot(guid))
            Fail(handler, name + " is a module bot that is not in the world yet.");
        else if (!guid.IsEmpty() && ObjectAccessor::FindPlayer(guid))
            Fail(handler, name + " is a real player's character: the .tb test commands change module bots and their bot-session masters only (the core's own GM commands are for real players).");
        else
            Fail(handler, name + " is not a logged-in bot.");
        return nullptr;
    }

    // .tb level <bot> <n>: the core's own level-up path, the one `.character level` takes (cs_character.cpp:914-917): GiveLevel (stats, health and
    // mana, and what the core grants on a level), then InitTalentForLevel (the talent points of the level) and the experience bar to 0.
    static bool HandleLevel(ChatHandler* handler, std::string name, uint8 level)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        if (level < 1 || level > 60)
            return Fail(handler, "the level must be 1 to 60.");
        bot->GiveLevel(level);
        bot->InitTalentForLevel();
        bot->SetXP(0);
        handler->PSendSysMessage("TortoiseBots: %s is level %u: health %u/%u, mana %d/%d.", name.c_str(), uint32(bot->GetLevel()),
            uint32(bot->GetHealth()), uint32(bot->GetMaxHealth()), bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA));
        return true;
    }

    // .tb train <bot>: teaches the bot, free and without an NPC, the class spells it may know at its level, from the core's own data
    // (delegates to tbhost::autotrain::TeachClassSpells).
    static bool HandleTrain(ChatHandler* handler, std::string name)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        tbhost::autotrain::Tally tally;
        tbhost::autotrain::TeachClassSpells(bot, tally);
        handler->PSendSysMessage("TortoiseBots: train: %u retail-era chainless class-line spell(s) not taught, %u retail-era trainer spell(s) taken away again: %s",
            tally.retailSkipped, tally.removedCount, tally.removed.c_str());
        handler->PSendSysMessage("TortoiseBots: %s learned %u spell(s) from %u class trainer(s) and %u from the class skill lines, in %u pass(es); trainer pass not taught: %u known, %u other class/race, %u level, %u ability, %u skill line, %u other.",
            name.c_str(), tally.trainerTaught, tally.trainers, tally.lineTaught, tally.passes,
            tally.trainerTally.known, tally.trainerTally.class_race, tally.trainerTally.level, tally.trainerTally.ability, tally.trainerTally.skill_line, tally.trainerTally.other);
        return true;
    }

    // .tb mount <bot> [status|train|buy]: what the bot still needs and the nearest riding trainer / racial mount vendor;
    // `train`/`buy` pay that NPC now and require the bot to stand within interaction range. The bot's own money is spent;
    // nothing is granted.
    static bool HandleMount(ChatHandler* handler, std::string name, Optional<std::string> verbArg)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;

        std::string const verb = verbArg ? Lower(*verbArg) : std::string("status");
        if (verb != "status" && verb != "train" && verb != "buy")
            return Fail(handler, "usage: .tb mount <bot> [status|train|buy].");

        float const range = (verb == "status") ? 150.0f : (INTERACTION_DISTANCE + 1.0f);
        tbhost::mountshop::Plan const plan = tbhost::mountshop::FindPlan(bot, range);
        tbhost::mountshop::Need const need = tbhost::mountshop::NeedOf(bot);
        char const* needText = need == tbhost::mountshop::Need::TrainRiding ? "riding"
            : need == tbhost::mountshop::Need::BuyMount ? "mount" : "none";
        handler->PSendSysMessage("TortoiseBots: mount %s: level %u, riding %u/%u, need %s.",
            bot->GetName().c_str(), uint32(bot->GetLevel()), uint32(bot->GetSkillValue(SKILL_RIDING)),
            uint32(tbhost::mountshop::RidingTargetForLevel(bot->GetLevel())), needText);

        if (plan.step == tbhost::mountshop::Step::TrainRiding)
        {
            Creature* trainer = ObjectAccessor::GetCreature(*bot, plan.npc);
            handler->PSendSysMessage("TortoiseBots: mount train: %s (%u) %.1f yd, cost %u copper.",
                trainer ? trainer->GetName().c_str() : "?", trainer ? trainer->GetEntry() : 0,
                trainer ? bot->GetDistance(trainer) : 0.0f, plan.cost);
        }
        else if (plan.step == tbhost::mountshop::Step::BuyMount)
        {
            Creature* vendor = ObjectAccessor::GetCreature(*bot, plan.npc);
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(plan.itemId);
            handler->PSendSysMessage("TortoiseBots: mount buy: %s (%u) %.1f yd, item %u \"%s\", cost %u copper.",
                vendor ? vendor->GetName().c_str() : "?", vendor ? vendor->GetEntry() : 0,
                vendor ? bot->GetDistance(vendor) : 0.0f, plan.itemId,
                proto ? proto->GetName(LOCALE_enUS) : "?", plan.cost);
        }
        else if (plan.step == tbhost::mountshop::Step::UseItem)
            handler->PSendSysMessage("TortoiseBots: mount use: item %u is in the bags.", plan.itemId);
        else
            handler->PSendSysMessage("TortoiseBots: mount plan: %s.", plan.reason.c_str());

        if (verb == "status")
            return true;
        if (plan.step == tbhost::mountshop::Step::None)
            return Fail(handler, "mount " + verb + ": " + plan.reason + ".");
        if (verb == "train" && plan.step != tbhost::mountshop::Step::TrainRiding)
            return Fail(handler, "mount train: riding is already trained; use `mount buy`.");
        if (verb == "buy" && plan.step == tbhost::mountshop::Step::TrainRiding)
            return Fail(handler, "mount buy: learn riding first (mount train).");

        uint32 spent = 0;
        std::string refusal;
        bool const ok = tbhost::mountshop::ExecutePlan(bot, plan, &spent, &refusal);
        if (!ok)
            return Fail(handler, "mount " + verb + " refused: " + refusal + ".");
        handler->PSendSysMessage("TortoiseBots: mount %s: ok, %u copper spent, riding %u/%u.", verb.c_str(), spent,
            uint32(bot->GetSkillValue(SKILL_RIDING)), uint32(tbhost::mountshop::RidingTargetForLevel(bot->GetLevel())));
        return true;
    }

    // .tb hurt <character> <percent> (the name left out: the sender's own character): sets the character's health to that percent of its maximum (1 at
    // least). A module bot, or the sender's own character (TB-1); never another real player.
    static bool HandleHurt(ChatHandler* handler, Tail args)
    {
        if (!RequireEngine(handler))
            return false;
        std::vector<std::string> const words = WordsOf(args);
        size_t const first = !words.empty() && target::IsNumberWord(words[0]) ? 0 : 1;
        if (words.size() != first + 1)
            return Fail(handler, "usage: .tb hurt <character> <percent>.");
        std::string code, detail;
        Player* player = TortoiseBots::h3::ResolveTestTarget(handler, first ? std::string_view(words[0]) : std::string_view(), code, detail);
        if (!player)
            return Fail(handler, detail + ".");
        if (!target::IsNumberWord(words[first]))
            return Fail(handler, "the percent must be 0 to 100.");
        uint32 const percent = uint32(std::strtoul(words[first].c_str(), nullptr, 10));
        if (percent > 100)
            return Fail(handler, "the percent must be 0 to 100.");
        if (!player->IsAlive())
            return Fail(handler, std::string(player->GetName()) + " is dead.");
        player->SetHealth(std::max<uint32>(1u, uint32(uint64(player->GetMaxHealth()) * percent / 100)));
        handler->PSendSysMessage("TortoiseBots: %s health is %u/%u.", player->GetName().c_str(), uint32(player->GetHealth()), uint32(player->GetMaxHealth()));
        return true;
    }

    // .tb power <character> mana|rage|energy <value> (the name left out: the sender's own character): sets that power to <value> percent of its
    // maximum (rage: percent of 100 = rage points, the unit `.tb rage` takes, which stays as the older spelling). The core stores rage times ten;
    // SetPower takes core units (Unit.h:849). A module bot, or the sender's own character (TB-1); never another real player.
    static bool HandlePower(ChatHandler* handler, Tail args)
    {
        if (!RequireEngine(handler))
            return false;
        std::vector<std::string> const words = WordsOf(args);
        size_t const first = !words.empty() && target::IsPowerWord(TortoiseBots::h3::Lower(words[0])) ? 0 : 1;
        if (words.size() != first + 2)
            return Fail(handler, "usage: .tb power <character> mana|rage|energy <percent>.");
        std::string code, detail;
        Player* bot = TortoiseBots::h3::ResolveTestTarget(handler, first ? std::string_view(words[0]) : std::string_view(), code, detail);
        if (!bot)
            return Fail(handler, detail + ".");
        std::string const kind = TortoiseBots::h3::Lower(words[first]);
        Powers power;
        if (kind == "mana")
            power = POWER_MANA;
        else if (kind == "rage")
            power = POWER_RAGE;
        else if (kind == "energy")
            power = POWER_ENERGY;
        else
            return Fail(handler, "usage: .tb power <character> mana|rage|energy <percent>.");
        if (!target::IsNumberWord(words[first + 1]))
            return Fail(handler, "the percent must be 0 to 100.");
        uint32 const value = uint32(std::strtoul(words[first + 1].c_str(), nullptr, 10));
        int32 const max = bot->GetMaxPower(power);
        if (max <= 0)
            return Fail(handler, std::string(bot->GetName()) + " has no " + kind + ".");
        bot->SetPower(power, int32(int64(max) * std::min<uint32>(value, 100) / 100));
        handler->PSendSysMessage("TortoiseBots: %s %s is %d/%d (core units).", bot->GetName().c_str(), kind.c_str(), bot->GetPower(power), max);
        return true;
    }

    // .tb additem <bot> <item id> [count]: a new item into the bags (Player::StoreNewItemInBestSlots, Player.h:1627). The id is looked up
    // with `.tb item <id>` first: no id is known from memory.
    static bool HandleAddItem(ChatHandler* handler, std::string name, uint32 entry, Optional<uint32> count)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        if (!sObjectMgr->GetItemTemplate(entry))
            return Fail(handler, "item " + std::to_string(entry) + " does not exist.");
        uint32 const n = std::max<uint32>(1u, count.value_or(1u));
        if (!bot->StoreNewItemInBestSlots(entry, n, ItemContext::NONE))
            return Fail(handler, name + ": the item was not stored (bags full?).");
        handler->PSendSysMessage("TortoiseBots: %s now has %u of item %u.", name.c_str(), bot->GetItemCount(entry), entry);
        return true;
    }

    // .tb castlog <bot> [on|off]: with a word, turns the bot's cast-result sink on or off (a bot without an engine has it off); without,
    // prints a copy of the queued cast results of the bot, never draining the engine's queue (R5 item 6; spike U3 and U1, SLICE2-R0-NOTES.md section 5).
    static bool HandleCastLog(ChatHandler* handler, std::string name, Optional<std::string> mode)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        auto* session = dynamic_cast<TortoiseBots::BotSession*>(bot->GetSession());
        if (!session)
            return Fail(handler, name + " has no bot session.");
        if (mode)
        {
            if (*mode != "on" && *mode != "off")
                return Fail(handler, "usage: .tb castlog <character> [on|off].");
            session->SetCastResultSink(*mode == "on");
            handler->PSendSysMessage("TortoiseBots: %s cast-result sink %s.", name.c_str(), mode->c_str());
            return true;
        }
        std::vector<TortoiseBots::CastResult> results;
        session->PeekCastResults(results);
        for (TortoiseBots::CastResult const& r : results)
        {
            if (r.kind == TortoiseBots::CastResult::Kind::SpellDelayed)
                handler->PSendSysMessage("TortoiseBots: kind=%s delay=%u%s", CastResultKindName(r.kind), r.delayMs, r.injected ? " injected" : "");
            else
                handler->PSendSysMessage("TortoiseBots: kind=%s spell=%u reason=%u cast=%s%s", CastResultKindName(r.kind),
                    r.spellId, r.reason, r.castId.ToString().c_str(), r.injected ? " injected" : "");
        }
        handler->PSendSysMessage("TortoiseBots: %s: %u queued result(s) (a copy, the queue keeps them); sink %s; moving now %s; casting now %s.", name.c_str(), uint32(results.size()),
            session->CastResultSink() ? "on" : "off", bot->isMoving() ? "yes" : "no",
            bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) ? "yes" : "no");
        return true;
    }

    // .tb use <bot> <item id> [self]: the bot uses an item from its bags through the core's own item cast (Player::CastItemUseSpell, Player.h:2684:
    // the call a client's use-item packet ends in, Player.cpp:33996), spike U1. Item id 0 only reports. Prints the item's facts first (legacy
    // flag, effects, on-use spell: a legacy item's effect is ignored by the core, Player.cpp:9814-9840), then mana, current cast and auras.
    // With the word `self` the cast carries the bot as its unit target; without, an empty target list (what a client sends for a self spell).
    static bool HandleUse(ChatHandler* handler, std::string name, uint32 entry, Optional<std::string> mode)
    {
        if (!RequireEngine(handler))
            return false;
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        if (entry)
        {
            Item* item = bot->GetItemByEntry(entry, ItemSearchLocation::Inventory);
            if (!item)
                return Fail(handler, "item " + std::to_string(entry) + " is not in the bags.");
            uint32 spellId = 0;
            for (ItemEffectEntry const* effect : item->GetEffects())
                if (effect->TriggerType == ITEM_SPELLTRIGGER_ON_USE) { spellId = effect->SpellID; break; }
            handler->PSendSysMessage("TortoiseBots: item %u: legacy %s, %u effect(s), on-use spell %u, count %u.", entry,
                item->GetTemplate()->HasFlag(ITEM_FLAG_LEGACY) ? "yes" : "no", uint32(item->GetEffects().size()), spellId, bot->GetItemCount(entry));
            if (!spellId)
                return Fail(handler, "the item has no on-use effect.");
            SpellCastTargets targets;
            if (mode && *mode == "self")
                targets.SetUnitTarget(bot);
            bot->CastItemUseSpell(item, spellId, targets, ObjectGuid::Empty, { 0, 0, 0 });
        }
        std::string auras;
        for (auto const& [id, app] : bot->GetAppliedAuras())
            auras += std::to_string(id) + " ";
        handler->PSendSysMessage("TortoiseBots: %s mana %d/%d, casting: %s, moving: %s, auras: %s", name.c_str(), bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA),
            bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) ? "yes" : "no", bot->isMoving() ? "yes" : "no", auras.c_str());
        return true;
    }

    // .tb perf: the tick cost since the last `.tb perf` (or the start), then a new window.
    static bool HandlePerf(ChatHandler* handler)
    {
        PerfWindow& perf = Perf();
        if (!perf.ticks)
        {
            handler->PSendSysMessage("TortoiseBots: perf: no tick since the window opened.");
            return true;
        }
        handler->PSendSysMessage("TortoiseBots: perf: %llu ticks; world update time avg %.2f ms, worst %u ms; module (sessions + engines) avg %.0f us, worst %u us; %u bot session(s).",
            (unsigned long long)perf.ticks, double(perf.worldSumMs) / double(perf.ticks), perf.worldMaxMs, double(perf.botSumUs) / double(perf.ticks),
            perf.botMaxUs, uint32(BotMgr::Instance().Count()));
        handler->PSendSysMessage("TortoiseBots: budget: %u us (pool %u + combat %u; target %u ms, smoothed %.2f ms); %llu of %llu pool turn(s) served, %llu tick(s) cut short, %llu held (core-bound).",
            perf.lastBudgetUs, perf.lastPoolUs, perf.lastCombatUs, perf.targetMs, perf.smoothedMs,
            (unsigned long long)perf.processedSum, (unsigned long long)perf.poolBotsSum,
            (unsigned long long)perf.cutTicks, (unsigned long long)perf.heldTicks);
        perf.Reset();
        return true;
    }

    // .tb spells <character>: the spells the bot's spell map holds (id and name), what the engine's "spell id" value searches.
    static bool HandleSpells(ChatHandler* handler, std::string name)
    {
        Player* bot = RequireBot(handler, name, false);
        if (!bot)
            return false;
        std::string line;
        uint32 count = 0;
        for (auto const& [spellId, spell] : bot->GetSpellMap())
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
            ++count;
            line += std::to_string(spellId) + ":" + (info ? info->SpellName->Str[LOCALE_enUS] : "?") + (spell.active ? "" : "(inactive)") + (info && info->IsPassive() ? "(passive)" : "") + " ";
            if (line.size() > 700)
            {
                handler->PSendSysMessage("TortoiseBots: spells: %s", line.c_str());
                line.clear();
            }
        }
        handler->PSendSysMessage("TortoiseBots: spells: %s", line.c_str());
        handler->PSendSysMessage("TortoiseBots: %s knows %u spell(s).", name.c_str(), count);
        return true;
    }

    // .tb ranks <class name|id>: the spell rank chains of the class's skill lines, as the chain table (TbSpellEntry.cpp ChainTable) holds them: for each
    // class skill line (skill category 7 with a SkillRaceClassInfo row for the class) the abilities whose ClassMask fits the class, grouped by chain
    // (`chain <line> <source> <name>: id:rank:level ...`, source core = the core's own chain entry, links = SkillLineAbility supersedes links, trainer = trainer_spell required-ability links of the same ability) and the
    // abilities of the line that are in no chain (`single <line>: id:level[:t<trainer level>]:name:family`, rank 0). It prints facts only; nothing is changed.
    static bool HandleRanks(ChatHandler* handler, std::string classText)
    {
        if (classText == "selftest")
        {
            uint32 failed = 0;
            for (std::string const& line : tbcompat::SpellChainSelfTest())
            {
                failed += line.rfind("FAIL", 0) == 0;
                handler->PSendSysMessage("TortoiseBots: ranks selftest: %s", line.c_str());
            }
            handler->PSendSysMessage("TortoiseBots: ranks selftest: %s", failed ? "FAILED" : "all cases PASS");
            return failed == 0;
        }
        if (classText == "lost")
        {
            uint32 n = 0;
            tbcompat::ForEachLostChain([&](std::vector<uint32> const& ids)
            {
                std::string text = "lost " + std::to_string(++n) + " " + (sSpellMgr->GetSpellInfo(ids.front(), DIFFICULTY_NONE) && sSpellMgr->GetSpellInfo(ids.front(), DIFFICULTY_NONE)->SpellName &&
                    (*sSpellMgr->GetSpellInfo(ids.front(), DIFFICULTY_NONE)->SpellName)[LOCALE_enUS] ? (*sSpellMgr->GetSpellInfo(ids.front(), DIFFICULTY_NONE)->SpellName)[LOCALE_enUS] : "?") + ":";
                for (uint32 id : ids)
                    text += " " + std::to_string(id);
                handler->PSendSysMessage("TortoiseBots: ranks: %s", text.c_str());
            });
            handler->PSendSysMessage("TortoiseBots: ranks: %u chain(s) of the loose trainer rule are not in the table.", n);
            return true;
        }
        static char const* const names[] = { "", "warrior", "paladin", "hunter", "rogue", "priest", "deathknight", "shaman", "mage", "warlock", "monk", "druid", "demonhunter", "evoker" };
        uint32 classId = 0;
        for (uint32 i = 1; i < std::size(names); ++i)
            if (classText == names[i] || classText == std::to_string(i))
                classId = i;
        if (!classId)
            return Fail(handler, "usage: .tb ranks <class name or id> (warrior, priest, ...).");
        uint32 const classBit = 1u << (classId - 1);
        auto onLine = [&](std::string const& text) { handler->PSendSysMessage("TortoiseBots: ranks: %s", text.c_str()); };
        // The level at which a class trainer teaches each spell (the lowest ReqLevel over the class's trainers): printed as `t<level>` next to the spell
        // level, because the data collaborator's lists give the level a spell is learned at.
        std::unordered_map<uint32, uint32> trainerLevel;
        for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
            if (trainer.GetType() == Trainer::Type::None && trainer.GetClassMask() == classBit)
                for (Trainer::Spell const& spell : trainer.GetSpells())
                {
                    auto const found = trainerLevel.find(spell.SpellId);
                    if (found == trainerLevel.end() || spell.ReqLevel < found->second)
                        trainerLevel[spell.SpellId] = spell.ReqLevel;
                }
        auto levelText = [&](uint32 id, SpellInfo const* info) { auto const found = trainerLevel.find(id); return std::to_string(info ? info->SpellLevel : 0) + (found == trainerLevel.end() ? std::string() : ":t" + std::to_string(found->second)); };
        onLine("class " + std::to_string(classId) + " " + names[classId] + "; " + tbcompat::SpellChainSummary());
        auto sourceText = [](uint8 bits)
        {
            std::string text;
            if (bits & tbcompat::SpellChainSource::Core)
                text += "core";
            if (bits & tbcompat::SpellChainSource::SkillLine)
                text += std::string(text.empty() ? "" : "+") + "links";
            if (bits & tbcompat::SpellChainSource::Trainer)
                text += std::string(text.empty() ? "" : "+") + "trainer";
            return text;
        };
        for (SkillLineEntry const* line : sSkillLineStore)
        {
            if (!line || line->CategoryID != SKILL_CATEGORY_CLASS)
                continue;
            bool classLine = false;
            for (SkillRaceClassInfoEntry const* info : sDB2Manager.GetSkillRaceClassInfo(line->ID))
                if (info->ClassMask && (uint32(info->ClassMask) & classBit))
                    classLine = true;
            if (!classLine)
                continue;
            std::vector<SkillLineAbilityEntry const*> const* abilities = sDB2Manager.GetSkillLineAbilitiesBySkill(line->ID);
            if (!abilities)
                continue;
            std::string const lineName = std::string(line->DisplayName[LOCALE_enUS] ? line->DisplayName[LOCALE_enUS] : "?");
            std::set<uint32> inLine;
            for (SkillLineAbilityEntry const* row : *abilities)
                if (row->Spell > 0 && (!row->ClassMask || (uint32(row->ClassMask) & classBit)))
                    inLine.insert(uint32(row->Spell));
            onLine("skill line " + std::to_string(line->ID) + " " + lineName + ": " + std::to_string(inLine.size()) + " abilities");
            std::set<uint32> chained;
            tbcompat::ForEachSpellChain([&](tbcompat::SpellChainList const& chain)
            {
                bool any = false;
                for (uint32 id : chain.ids)
                    if (inLine.count(id))
                        any = true;
                if (!any)
                    return;
                SpellInfo const* first = sSpellMgr->GetSpellInfo(chain.ids.front(), DIFFICULTY_NONE);
                std::string text = "chain " + lineName + " " + sourceText(chain.sources) + " " +
                    (first && first->SpellName && (*first->SpellName)[LOCALE_enUS] ? (*first->SpellName)[LOCALE_enUS] : "?") + ":";
                for (uint32 id : chain.ids)
                {
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
                    chained.insert(id);
                    text += " " + std::to_string(id) + ":" + std::to_string(tbcompat::SpellRankOf(id)) + ":" + levelText(id, info) + (inLine.count(id) ? "" : "(not in line)");
                    if (text.size() > 600)
                    {
                        onLine(text);
                        text = "chain-cont:";
                    }
                }
                onLine(text);
            });
            for (uint32 id : inLine)
            {
                if (chained.count(id))
                    continue;
                SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
                if (!info)
                    continue;
                onLine("single " + lineName + ": " + std::to_string(id) + ":" + levelText(id, info) + ":" +
                    (info->SpellName && (*info->SpellName)[LOCALE_enUS] ? (*info->SpellName)[LOCALE_enUS] : "?") + ":" + std::to_string(info->SpellFamilyName));
            }
        }
        // The trainer links that were prerequisites and not ranks, for the abilities of this class.
        std::set<uint32> const classSpells = tbcompat::ClassLineSpells(classId);
        tbcompat::ForEachRejectedTrainerLink([&](uint32 required, uint32 taught, char const* why)
        {
            if (classSpells.count(taught))
                onLine("prerequisite (not a rank): " + std::to_string(taught) + " requires " + std::to_string(required) + " [" + why + "]");
        });
        tbcompat::ForEachQuarantinedComponent([&](std::vector<uint32> const& ids)
        {
            bool inClass = false;
            for (uint32 id : ids)
                inClass = inClass || classSpells.count(id);
            if (inClass)
            {
                std::string text = "quarantined component:";
                for (uint32 id : ids)
                    text += " " + std::to_string(id);
                onLine(text);
            }
        });
        onLine("done");
        return true;
    }

    // .tb unported: every feature of the legacy engine that is not ported yet, with how many times the engine reached it.
    static bool HandleUnported(ChatHandler* handler)
    {
#ifdef TORTOISEBOTS_SLICE1
        std::string const report = tbcompat::UnportedReport();
#else
        std::string const report = "the engine is not built in (this build has no list of unported features)";
#endif
        size_t begin = 0;
        while (begin < report.size())
        {
            size_t end = report.find('\n', begin);
            if (end == std::string::npos)
                end = report.size();
            handler->PSendSysMessage("TortoiseBots: %s", report.substr(begin, end - begin).c_str());
            begin = end + 1;
        }
        return true;
    }

    // .tb engine <character> on|off: run the bot on the real engine (PlayerbotAI) or on the spike's follow/assist.
    static bool HandleEngine(ChatHandler* handler, std::string name, std::string mode)
    {
        bool on;
        if (mode == "on")
            on = true;
        else if (mode == "off")
            on = false;
        else
            return Fail(handler, "usage: .tb engine <character> on|off.");
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        std::string const error = BotMgr::Instance().SetEngine(guid, on);
        if (!error.empty())
            return Fail(handler, name + ": engine not changed: " + error + ".");
        handler->PSendSysMessage("TortoiseBots: %s engine %s.", name.c_str(), on ? "on" : "off");
        return true;
    }

    // .tb logout <character>
    static bool HandleLogout(ChatHandler* handler, std::string name)
    {
        ObjectGuid const guid = GuidByName(name);
        if (!RequireOwnedBot(handler, guid))
            return false;
        if (!BotMgr::Instance().Logout(guid, "command"))
        {
            handler->PSendSysMessage("TortoiseBots: %s is not a logged-in bot.", name.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->PSendSysMessage("TortoiseBots: %s is logging out.", name.c_str());
        return true;
    }

    // .tb cast <character> <spell id>: the bot casts the spell on the caller's target (on the caller with no target).
    static bool HandleCast(ChatHandler* handler, std::string name, uint32 spellId)
    {
        Player* bot = RequireBot(handler, name);
        if (!bot)
            return false;
        if (!sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
        {
            handler->PSendSysMessage("TortoiseBots: spell %u does not exist.", spellId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Target: the caller's selection; from the console the bot's own victim; otherwise the bot itself.
        Unit* target = nullptr;
        if (Player* caller = Caller(handler))
            target = caller->GetSelectedUnit() ? caller->GetSelectedUnit() : caller;
        else
            target = bot->GetVictim() ? bot->GetVictim() : bot;

        SpellCastResult const result = bot->CastSpell(target, spellId);
        handler->PSendSysMessage("TortoiseBots: %s casts %u on %s: result %u (0 = ok).", name.c_str(), spellId,
            target->GetName().c_str(), uint32(result));
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("TortoiseBots: %s, %u bot session(s).", BotMgr::Instance().Enabled() ? "enabled" : "disabled",
            uint32(BotMgr::Instance().Count()));
        return true;
    }

    // .tb spellscan: build the spell mirror for every DIFFICULTY_NONE spell the core has, and report what it cost and
    // what the mirror could not hold. Read-only. An assert anywhere in the core accessors the fill makes would abort
    // here, which is what the check is for.
    static bool HandleSpellScan(ChatHandler* handler)
    {
        uint32 const started = getMSTime();
        uint32 spells = 0, incomplete = 0, moreEffects = 0, multiSchool = 0, effects = 0;
        uint32 nonNoneRecords = 0;
        std::string firstTen, moreEffectsIds;
        std::unordered_set<uint32> nonNoneIds, nonNoneOnlyIds;

        sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
        {
            if (!info)
                return;

            // The slot holds only DIFFICULTY_NONE records; the other variants are counted, and so are the ids that have
            // no NONE record at all (SpellProto answers those with nullptr).
            if (info->Difficulty != DIFFICULTY_NONE)
            {
                ++nonNoneRecords;
                if (nonNoneIds.insert(info->Id).second)
                {
                    SpellInfo const* none = sSpellMgr->GetSpellInfo(info->Id, DIFFICULTY_NONE);
                    if (!none || none->Difficulty != DIFFICULTY_NONE)
                        nonNoneOnlyIds.insert(info->Id);
                }
                return;
            }

            SpellEntry const* entry = tbcompat::SpellProto(info);
            if (!entry)
                return;
            ++spells;
            effects += uint32(info->GetEffects().size());
            if (entry->MoreEffects)
            {
                ++moreEffects;
                if (moreEffects <= 5)
                    moreEffectsIds += (moreEffectsIds.empty() ? "" : ", ") + std::to_string(entry->Id) + " (" + std::to_string(entry->MoreEffects) + ")";
            }
            if (std::bitset<32>(info->SchoolMask).count() > 1)
                ++multiSchool;
            if (entry->Incomplete)
            {
                ++incomplete;
                if (incomplete <= 10)
                    firstTen += (firstTen.empty() ? "" : "; ") + std::to_string(entry->Id) + " (" + entry->IncompleteReason + ")";
            }
        });

        std::string firstNonNoneOnly;
        for (uint32 id : nonNoneOnlyIds)
        {
            if (!firstNonNoneOnly.empty())
                firstNonNoneOnly += ", ";
            if (firstNonNoneOnly.size() < 200)
                firstNonNoneOnly += std::to_string(id);
            else
            {
                firstNonNoneOnly += "...";
                break;
            }
        }

        uint64 const indexBytes = tbcompat::SpellIndexBytes();
        uint64 const slotBytes = tbcompat::SpellSlotBytes();
        uint32 const ms = getMSTimeDiff(started, getMSTime());
        handler->PSendSysMessage("TortoiseBots: spell mirrors: %u spells in %u ms, spell id bound %u (index %llu B + slots %llu B), %u incomplete%s",
            spells, ms, tbcompat::MaxSpellId(), (unsigned long long)indexBytes, (unsigned long long)slotBytes,
            incomplete, incomplete ? (" (first: " + firstTen + ")").c_str() : "");
        handler->PSendSysMessage("TortoiseBots: effects: %u effect slots in all, %u spells with more than 3 effects%s, %u spells with more than one school.",
            effects, moreEffects, moreEffects ? (" (first: " + moreEffectsIds + ")").c_str() : "", multiSchool);
        handler->PSendSysMessage("TortoiseBots: difficulty: %u SpellInfo records with a difficulty other than NONE, %u of their ids have no DIFFICULTY_NONE record (mirror null; %u such lookups counted%s).",
            nonNoneRecords, uint32(nonNoneOnlyIds.size()), tbcompat::NonNoneOnlyLookups(),
            nonNoneOnlyIds.empty() ? "" : (" (first: " + firstNonNoneOnly + ")").c_str());
        return true;
    }

    // .tb spell <id>: the fields the spell mirror (TbSpellEntry.h) holds for one spell, for comparing it with the client's data.
    static bool HandleSpell(ChatHandler* handler, uint32 spellId)
    {
        SpellEntry const* entry = tbcompat::SpellProto(spellId);
        if (!entry)
            return Fail(handler, "spell " + std::to_string(spellId) + " does not exist.");

        if (entry->Incomplete)
            handler->PSendSysMessage("TortoiseBots: INCOMPLETE: %s", entry->IncompleteReason.c_str());

        char const* name = entry->SpellName[LOCALE_enUS].empty() ? "?" : entry->SpellName[LOCALE_enUS].c_str();
        char const* rank = entry->Rank[LOCALE_enUS].empty() ? "" : entry->Rank[LOCALE_enUS].c_str();

        // The range the bot looks up from rangeIndex, the same lookup (SERVER side: SpellRange.dbc).
        float minRange = 0.0f, maxRange = 0.0f;
        if (SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(entry->rangeIndex))
        {
            minRange = range->RangeMin[0];
            maxRange = range->RangeMax[0];
        }

        handler->PSendSysMessage("TortoiseBots: spell %u \"%s %s\": school %u (mask 0x%X), category %u, cast time %u ms, range %.1f-%.1f (idx %u), duration %d ms, levels %u/%u/%u (base/max/spell), recovery %u/%u ms, GCD %u ms (category %u), mana %u (%u%%), power %u.",
            entry->Id, name, rank, entry->School, entry->Core ? entry->Core->SchoolMask : 0u, entry->Category,
            entry->GetCastTime(nullptr), minRange, maxRange, entry->rangeIndex, entry->GetDuration(),
            entry->baseLevel, entry->maxLevel, entry->spellLevel,
            entry->RecoveryTime, entry->CategoryRecoveryTime, entry->StartRecoveryTime, entry->StartRecoveryCategory,
            entry->manaCost, entry->ManaCostPercentage, entry->powerType);

        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (!entry->Effect[i] && !entry->EffectApplyAuraName[i])
                continue;
            handler->PSendSysMessage("TortoiseBots: effect %u: type %u, aura %u, base points %d + 1, die sides %d, misc %d, target A/B %u/%u, radius %u, trigger %u, item %u, chain %u, amp %.0f, points/level %.2f.",
                i, entry->Effect[i], entry->EffectApplyAuraName[i], entry->EffectBasePoints[i], entry->EffectDieSides[i],
                entry->EffectMiscValue[i], entry->EffectImplicitTargetA[i], entry->EffectImplicitTargetB[i],
                entry->EffectRadiusIndex[i], entry->EffectTriggerSpell[i], entry->EffectItemType[i],
                entry->EffectChainTarget[i], entry->Core && i < entry->Core->GetEffects().size() ? entry->Core->GetEffects()[i].Amplitude : 0.0f,
                entry->EffectRealPointsPerLevel[i]);
        }

        handler->PSendSysMessage("TortoiseBots: targets 0x%X, family %u flags 0x%llX, attributes 0x%X/0x%X/0x%X, dispel %u, mechanic %u, stack %u, proc %u x%u, reagents %d/%u totem %u/%u, max targets %u, more effects %u.",
            entry->Targets, entry->SpellFamilyName, (unsigned long long)entry->SpellFamilyFlags, entry->Attributes,
            entry->AttributesEx, entry->AttributesEx2, entry->Dispel, entry->Mechanic, entry->StackAmount,
            entry->procChance, entry->procCharges, entry->Reagent[0], entry->ReagentCount[0], entry->Totem[0],
            entry->Totem[1], entry->MaxAffectedTargets, entry->MoreEffects);
        return true;
    }

    // .tb item <entry>: what the item mirror (TbItemPrototype.h) holds for one item, for comparing it with the database.
    static bool HandleItem(ChatHandler* handler, uint32 entry)
    {
        ItemPrototype const* proto = tbcompat::ItemProto(entry);
        if (!proto)
            return Fail(handler, "item " + std::to_string(entry) + " does not exist.");

        if (proto->Incomplete)
            handler->PSendSysMessage("TortoiseBots: INCOMPLETE: %s", proto->IncompleteReason.c_str());

        handler->PSendSysMessage("TortoiseBots: item %u \"%s\": class %u/%u, quality %u, inventory %u, item level %u, required level %u, buy %u sell %u, bag family %u (mask 0x%X).",
            proto->ItemId, proto->Name1.c_str(), proto->Class, proto->SubClass, proto->Quality, proto->InventoryType,
            proto->ItemLevel, proto->RequiredLevel, proto->BuyPrice, proto->SellPrice, proto->BagFamily, proto->BagFamilyMask);
        handler->PSendSysMessage("TortoiseBots: armor %u, block %u (base %u + stat %u, not added), damage %.1f-%.1f type %u, delay %u, bonding %u, max stack %u, durability %u.",
            uint32(proto->Armor), proto->Block, proto->BlockBase, proto->BlockStat, proto->Damage[0].DamageMin, proto->Damage[0].DamageMax,
            proto->Damage[0].DamageType, proto->Delay, proto->Bonding, proto->Stackable, proto->MaxDurability);

        std::string stats;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            if (proto->ItemStat[i].ItemStatValue)
                stats += (stats.empty() ? "" : ", ") + std::to_string(proto->ItemStat[i].ItemStatType) + ":" + std::to_string(proto->ItemStat[i].ItemStatValue);

        std::string spells;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            if (proto->Spells[i].SpellId)
                spells += (spells.empty() ? "" : ", ") + std::to_string(proto->Spells[i].SpellId) +
                    " (trigger " + std::to_string(proto->Spells[i].SpellTrigger) + ", charges " + std::to_string(proto->Spells[i].SpellCharges) + ", category " + std::to_string(proto->Spells[i].SpellCategory) + ")";

        handler->PSendSysMessage("TortoiseBots: stats %s; spells %s; resistances %u/%u/%u/%u/%u/%u.",
            stats.empty() ? "none" : stats.c_str(), spells.empty() ? "none" : spells.c_str(),
            uint32(proto->HolyRes), uint32(proto->FireRes), uint32(proto->NatureRes), uint32(proto->FrostRes),
            uint32(proto->ShadowRes), uint32(proto->ArcaneRes));
        return true;
    }

    // W7 test helper ONLY: .tb setskill <bot> <skill> <value> [max]
    static bool HandleSetSkill(ChatHandler* handler, Tail args)
    {
        std::vector<std::string> const words = WordsOf(args);
        if (words.size() < 3)
            return Fail(handler, "usage: .tb setskill <bot> <skill> <value> [max]");
        ObjectGuid const guid = GuidByName(words[0]);
        Player* bot = guid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(guid);
        if (!bot || !BotMgr::Instance().IsBot(guid))
            return Fail(handler, "bot '" + words[0] + "' is not online.");
        uint32 const skillId = uint32(std::strtoul(words[1].c_str(), nullptr, 10));
        uint16 const val = uint16(std::strtoul(words[2].c_str(), nullptr, 10));
        uint16 const maxVal = words.size() >= 4 ? uint16(std::strtoul(words[3].c_str(), nullptr, 10)) : std::max<uint16>(75, val);
        uint16 const step = maxVal >= 300 ? 4 : maxVal >= 225 ? 3 : maxVal >= 150 ? 2 : 1;
        bot->SetSkill(skillId, step, val, maxVal);
        handler->PSendSysMessage("TortoiseBots: %s skill %u set to %u/%u (step %u).", bot->GetName().c_str(), skillId, val, maxVal, step);
        return true;
    }
};
}

void AddSC_tortoisebots()
{
    new tortoisebots_world();
    new tortoisebots_player();
    new tortoisebots_unit();
    new tortoisebots_group();
    new tortoisebots_server();
    new tortoisebots_commands();
    TortoiseBots::chatin::AddScripts();     // S1-R2: PlayerScript::OnChat -> the bots' chat inboxes (host/chat/TbChatIn.cpp)
    TortoiseBots::player::AddScripts();     // R1: PlayerScript::OnCommand -> the players' `.bot` entry (host/player/TbPlayerCmd.cpp)
#ifdef TORTOISEBOTS_SLICE1
    TortoiseBots::hire::RegisterRecruiterScripts();   // #298 P3: the <Mercenary Hire> NPC gossip (host/hire/TbHireRecruiter.cpp)
#endif
    TortoiseBots::addon::AddScripts();      // #130: PlayerScript::OnAddonMessageSent -> the TBM addon channel (host/TbAddonChannel.cpp)
    TortoiseBots::addoninfo::RegisterAddonVerbsInfo(); // #130 f-info: the "features" and "run" verbs (host/TbAddonInfo.cpp)
    TortoiseBots::action::RegisterAddonVerbsActions();   // #130 f-actions: the `action` verb + `action:<intent>` tokens
    TortoiseBots::totem::RegisterAddonVerbsTotem();   // #133 c-totem: the `totem` verb + `totem` cap (host/TbAddonTotem.cpp)
    TortoiseBots::addonverbs::RegisterAddonVerbsPassthrough();   // #130 f-verbs: the `.bot` words the player layer answers (host/TbAddonVerbs.cpp)
    TortoiseBots::roster::RegisterAddonVerbsRoster();   // #130: `roster`/`list`/`alts` + handshake/BOTSTATE over the TBM channel (host/TbAddonRoster.cpp)
    TortoiseBots::blessing::RegisterAddonVerbsBlessing(); // #133 c-blessing: `blessing` verb + `blessing`/`blessing-player` caps (host/TbAddonBlessing.cpp)
    TortoiseBots::choice::RegisterAddonVerbsChoice();   // #133 c-choice: `choice` verb + `choice` cap (host/TbAddonChoice.cpp)
    TortoiseBots::behsvc::RegisterAddonVerbsBehaviorService(); // #133 c-behsvc: `behavior` + `service` addon verbs
    TortoiseBots::inv::RegisterAddonVerbsInv();   // #134 i-inv: the `inv`/`inventory`/`item` verbs + `inventory` cap (host/TbAddonInv.cpp)
    TortoiseBots::trade::RegisterAddonVerbsTrade(); // #134 P134-C: `trade` addon verb + trade window driver
    TortoiseBots::professions::RegisterAddonVerbsProfessions(); // #134 P134-D: `professions` verb + `professions` cap (host/TbAddonProfessions.cpp)
    TortoiseBots::lootlog::RegisterAddonVerbsLootLog(); // #134 i-loot: `lootlog` verb + `loot-log` cap (host/TbAddonLootLog.cpp)
    TortoiseBots::aipreset::RegisterAddonVerbsAi(); // #135 P135-C: `ai` verb + `ai` cap (host/TbAddonAi.cpp)
    TortoiseBots::addonorders::RegisterAddonVerbsOrders(); // #155: `order` verb + guild-order caps (host/TbAddonOrders.cpp)
    TortoiseBots::autogear::AddScripts();   // PlayerScript::OnCommand -> `.autogear`
    TortoiseBots::cmd::combat::RegisterScripts();   // P5: the damage, heal, death and cast hooks of the fight monitor
    TortoiseBots::bg::RegisterScripts();            // BG1: the battleground queue join, invite and port-failed hooks (inert while off)
    TortoiseBots::lfg::RegisterScripts();           // #160: the dungeon-finder fill tick (inert while off)
#ifdef TORTOISEBOTS_SLICE1
    TortoiseBots::hire::worldbuffs::AddScripts();   // world-buff aura/kill credit and the raid-entry rule (engine build only)
    TortoiseBots::hire::RegisterScripts();          // P4: the contract kill credit (boss kills, kill-store load/drop)
#endif
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1 && defined(TBF_WITH_FOREVER_LUA) && TBF_WITH_FOREVER_LUA
    TortoiseBots::luabots::Register();              // B1: the ForeverLua bot API provider (host/TbLuaBots.cpp); once, never unregistered
#endif
}
