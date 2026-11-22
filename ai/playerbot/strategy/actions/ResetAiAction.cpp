
#include "playerbot/playerbot.h"
#include "ResetAiAction.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/TbCompatW3.h"

using namespace ai;

// Defined in Helpers.cpp.
std::string& trim(std::string& s);

bool ResetAiAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    ResetValues();
    ResetStrategies();

    if (fullReset)
    {
        ai->TellError(requester, "AI was reset to defaults");
    }

    return true;
}

void ResetAiAction::ResetStrategies()
{
    bool loadStrats = !fullReset;

    ai->ResetStrategies(loadStrats);

    if (fullReset)
        sPlayerbotDbStore.Save(ai);
}

void ResetAiAction::ResetValues()
{
    uint64 guid = DbStoreBotGuidCompat(ai->GetBot());

    auto results = QueryGuardCompat(DbStoreQueryValuesCompat(guid));
    if (results)
    {
        std::list<std::string> values;
        do
        {
            Field* fields = results->Fetch();
            std::string val = fields[0].GetString();

            std::vector<std::string> parts = split(val, '>');
            if (parts.size() != 2) continue;

            std::string name = parts[0];
            std::string text = parts[1];

            UntypedValue* value = context->GetUntypedValue(name);
            if (!value) continue;

            if (fullReset)
                value->Reset();
            else
                value->Load(text);

        } while (results->NextRow());

        if (fullReset)
            DbStoreDeleteValuesCompat(guid);
    }
}

bool SaveAiAction::Execute(Event& event)
{
   std::string preset = event.GetParam();
   trim(preset);

   if (preset.empty())
   {
      preset = getQualifier();
      trim(preset);
   }

   sPlayerbotDbStore.Save(ai, preset);

   if (Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster())
   {
      ai->TellError(requester, "Strategies saved");
   }

   return true;
}

bool LoadAiAction::Execute(Event& event)
{
   std::string preset = event.GetParam();
   trim(preset);

   if (preset.empty())
   {
      preset = getQualifier();
      trim(preset);
   }

   sPlayerbotDbStore.Load(ai, preset);

   if (Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster())
   {
      ai->TellError(requester, "Strategies loaded");
   }

   return true;
}

bool ListAiAction::Execute(Event& event)
{
   if (Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster())
   {
      uint64 guid = DbStoreBotGuidCompat(ai->GetBot());

      ai->TellError(requester, "### PRESETS AVAILABLE ###");

      auto results = QueryGuardCompat(DbStoreQueryPresetsCompat(guid));
      if (results)
      {
         std::list<std::string> values;
         do
         {
            Field* fields = results->Fetch();
            std::string val = fields[0].GetString();
            ai->TellError(requester, val.empty() ? "(default)" : val.c_str());

         } while (results->NextRow());
      }
   }

   return true;
}

bool ResetStratsAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    ResetStrategies();

    if (fullReset)
    {
        ai->TellError(requester, "Strategies reset to defaults");
    }

    return true;
}

bool ResetValuesAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    ResetValues();

    if (fullReset)
    {
        ai->TellError(requester, "Settings reset to defaults");
    }

    return true;
}