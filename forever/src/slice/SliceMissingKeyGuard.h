// Slice 1, missing-key guard (FOREVER-SLICE1-SPEC section 8, risk 2 and 3).
//
// The slice registers 3 strategies, 10 actions, 27 triggers and 139 values (SliceAiObjectContext.cpp); the sources it
// compiles name more. The engine skips a missing strategy, trigger or action (Engine.cpp), but the value macros
// dereference a missing value (`context->GetValue<T>(name)->Get()`, strategy/ValueMacros.h), which would crash the
// server on the first bot tick that reaches one.
//
// GetStrategy / GetTrigger / GetAction / GetUntypedValue are virtual in AiObjectContext, so the slice's context
// (SliceWarriorAiObjectContext in SliceAiFactory.cpp) overrides them without touching a shared header:
//   - every miss is logged once per name: "slice: missing value <name>" (and strategy, trigger, action);
//   - a missing strategy, trigger or action stays null: the engine already skips those;
//   - a missing value gets an inert stand-in that answers every read with T{} and ignores writes.
//
// What a stand-in can be: `GetValue<T>` is a non-virtual template that does dynamic_cast<Value<T>*>(GetUntypedValue()),
// and the override cannot know T. So the stand-in is ONE object that derives from Value<T> for each T in
// SliceInertTypes below; the cast then finds the right base (a cross-cast). A value read as a type that is not listed
// still casts to null and crashes: the log line comes first, then the crash names the key. The list is the set of types
// the slice's own sources read the unregistered values as; forever/tools/slice_registry_check.py prints, for every
// unregistered value, its types and flags one this list lacks.
#pragma once

#include "SliceLog.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PatchwerkOpeningPolicy.h"
#include "playerbot/policy/combat/RunnerFinishPolicy.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/strategy/values/TravelValues.h"

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

namespace ai
{
namespace slice
{
    // One Value<T> that holds a T{} and never changes it.
    template <class T>
    class InertValue : public Value<T>
    {
    public:
        T Get() override { return value; }
        typename Value<T>::RefType GetRef() override { return value; }
        T LazyGet() override { return value; }
        void Set(T) override {}

    private:
        std::remove_reference_t<T> value{};
    };

    // R11: the one pointer type a caller WRITES through (`*AI_VALUE(FutureDestinations*, ...) = ...`, PlayerbotAI::Reset(true) at
    // PlayerbotAI.cpp:2719 and the travel chooser): a null stand-in made every full reset a SIGSEGV at address 0x8 (a master changing maps).
    // The key is registered now (SliceAiObjectContext.cpp), but a stand-in for it points at an owned, empty future instead of null, so a
    // missing key can never again be a null write. Reads of the other pointer types stay null (callers test them or the stubs ignore `this`).
    template <>
    class InertValue<FutureDestinations*> : public Value<FutureDestinations*>
    {
    public:
        FutureDestinations* Get() override { return &holder; }
        typename Value<FutureDestinations*>::RefType GetRef() override { return pointer; }
        FutureDestinations* LazyGet() override { return &holder; }
        void Set(FutureDestinations*) override {}

    private:
        FutureDestinations holder;
        FutureDestinations* pointer = &holder;
    };

    // The stand-in object: an UntypedValue that is also an InertValue<T> for every T in the list.
    template <class... Ts>
    class InertValues : public UntypedValue, public InertValue<Ts>...
    {
    public:
        InertValues(PlayerbotAI* ai, std::string const& name) : UntypedValue(ai, name) {}
        void Reset() override {}
        std::string Format() override { return "slice: inert stand-in"; }
        std::string Save() override { return "?"; }
        bool Load(std::string) override { return false; }
    };

    // The types the slice's sources read the unregistered values as (slice_registry_check.py parses this alias; keep
    // one type per line and no duplicates, a reference type and its value type are two entries).
    // Slice 2 (R3) adds ItemUsage (`item usage`, read by LootStrategyValue.cpp:25; T{} = ITEM_USAGE_NONE = "not looted", the answer that
    // makes the bot do less, decision D2) and std::set<uint32>& (`skip go loot list`, read in LootObjectStack.cpp: an empty list).
    // Slice 9 (L9) adds std::vector<uint32> (`craft spells`) and std::vector<MountValue> (`mount list`), read by ItemUsageValue.cpp: an empty list,
    // so no craft and no mount is planned (the usage then falls to the item's other classes: LESS). `item usage` itself is registered since slice 9.
    using SliceInertValue = InertValues<
        bool,
        uint8,
        uint32,
        Unit*,
        LootObjectStack*,
        FutureDestinations*,
        GuildOrder,
        LootObject,
        PatchwerkOpeningWindow,
        std::map<ObjectGuid, uint32>,
        std::map<ObjectGuid, uint32>&,
        std::map<uint32, uint32>&,
        std::map<ObjectGuid, RunnerLedgeEntry>&,
        std::list<ObjectGuid>,
        std::string,
        std::list<std::string>&,
        std::set<ObjectGuid>&,
        std::set<uint32>&,
        ItemUsage,
        std::vector<uint32>,
        std::vector<MountValue>>;

    // Each missing name is reported once per process (every bot has its own context, the names repeat).
    class MissingKeyLog
    {
    public:
        // `what` is "strategy", "trigger", "action" or "value". The qualifier ("name::param") is not part of the key.
        static void Report(char const* what, std::string const& name)
        {
            std::string const key = std::string(what) + " " + name.substr(0, name.find("::"));
            std::lock_guard<std::mutex> lock(Mutex());
            if (!Seen().insert(key).second)
                return;
            LogWarn(std::string("slice: missing ") + what + " " + name);
        }

        static size_t Count()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            return Seen().size();
        }

    private:
        static std::mutex& Mutex() { static std::mutex m; return m; }
        static std::set<std::string>& Seen() { static std::set<std::string> s; return s; }
    };

    // Wraps a class context (WarriorAiObjectContext for the slice): the lookups report their misses.
    template <class Base>
    class GuardedContext : public Base
    {
    public:
        explicit GuardedContext(PlayerbotAI* ai) : Base(ai) {}

        Strategy* GetStrategy(std::string const& name) override
        {
            Strategy* s = Base::GetStrategy(name);
            if (!s)
                MissingKeyLog::Report("strategy", name);
            return s;
        }

        Trigger* GetTrigger(std::string const& name) override
        {
            Trigger* t = Base::GetTrigger(name);
            if (!t)
                MissingKeyLog::Report("trigger", name);
            return t;
        }

        Action* GetAction(std::string const& name) override
        {
            Action* a = Base::GetAction(name);
            if (!a)
                MissingKeyLog::Report("action", name);
            return a;
        }

        UntypedValue* GetUntypedValue(std::string const& name) override
        {
            UntypedValue* v = Base::GetUntypedValue(name);
            if (v)
                return v;
            MissingKeyLog::Report("value", name);
            std::unique_ptr<SliceInertValue>& stand = standIns[name];
            if (!stand)
                stand = std::make_unique<SliceInertValue>(this->ai, name);
            return stand.get();
        }

    private:
        // Owned by the context, so they live as long as the bot's AI does. Keyed by the full name (qualifier included).
        std::map<std::string, std::unique_ptr<SliceInertValue>> standIns;
    };
}
}
