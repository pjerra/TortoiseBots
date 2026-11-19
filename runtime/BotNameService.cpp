#include "BotNameService.h"
#include "BotNameData.h"
#include "BotNames.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Log.h"
#include "Util.h"

#include <fstream>
#include <sstream>

namespace TortoiseBots
{

namespace
{

uint32_t Roll(uint32_t lo, uint32_t hi)
{
    return urand(lo, hi);
}

// Built-in pools plus AiPlayerbot.RandomBotNamesFile, read once on first use.
BotNamePools const& Pools()
{
    static BotNamePools const pools = []
    {
        BotNamePools built;
        std::string text;
        for (char const* section : kBotNameData)
            text += section;
        built.Parse(text);
        std::string const& path = sPlayerbotAIConfig.randomBotNamesFile;
        if (!path.empty())
        {
            std::ifstream file(path);
            if (!file)
                sLog.outError("TortoiseBots: AiPlayerbot.RandomBotNamesFile '%s' cannot be read; using the built-in names only", path.c_str());
            else
            {
                std::stringstream extra;
                extra << file.rdbuf();
                sLog.outString("TortoiseBots: added %u bot names from %s", uint32(built.Parse(extra.str())), path.c_str());
            }
        }
        return built;
    }();
    return pools;
}

// The pre-#1003 syllable names, kept only as the last resort.
std::string FallbackName()
{
    static char const* onset[] = {
        "b","br","c","cr","d","dr","f","g","gr","h","j","k","kr","l","m","n",
        "p","r","s","sh","st","t","th","tr","v","w","z","kh","gh","mor","thar","zar"
    };
    static char const* nucleus[] = { "a","e","i","o","u","ae","ar","or","ur","al","yr" };
    static char const* coda[]    = { "","","","n","r","k","l","th","sh","g","rn" };
    std::string name;
    for (uint32 i = 0, syllables = urand(2, 3); i < syllables; ++i)
    {
        name += onset[urand(0, sizeof(onset) / sizeof(onset[0]) - 1)];
        name += nucleus[urand(0, sizeof(nucleus) / sizeof(nucleus[0]) - 1)];
    }
    name += coda[urand(0, sizeof(coda) / sizeof(coda[0]) - 1)];
    if (name.size() > 11)
        name.resize(11);
    return NormalizeBotName(name);
}

bool UsableCharacterName(std::string const& name)
{
    return !name.empty() &&
        ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS &&
        !sObjectMgr.IsReservedName(name) &&
        !PlayerGuidByNameCompat(name);
}

} // namespace

std::string PickBotCharacterName(uint8_t race, uint8_t cls, uint8_t gender)
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        std::string name = Pools().Generate(race, cls, gender, sPlayerbotAIConfig.randomBotNameStyleWeights, Roll);
        if (UsableCharacterName(name))
            return name;
    }
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        std::string name = FallbackName();
        if (UsableCharacterName(name))
            return name;
    }
    return std::string();
}

std::string PickBotPetName()
{
    std::vector<std::string> const& names = Pools().Words("pet:hunter");
    for (int attempt = 0; attempt < 10 && !names.empty(); ++attempt)
    {
        std::string name = NormalizeBotName(names[urand(0, uint32(names.size() - 1))]);
        if (!name.empty() && ObjectMgr::CheckPetName(name) == PET_NAME_SUCCESS && !sObjectMgr.IsReservedName(name))
            return name;
    }
    return std::string();
}

} // namespace TortoiseBots
