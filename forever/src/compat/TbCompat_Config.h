// cmangos's `Config` on ForeverCore: a plain ini file reader with the Turtle class's interface and rules (slice 1, R3).
// Included from forever-compat-shim.h (which makes the name `Config` mean this class; `sConfig` stays the core's store).
//
// Why a class of our own. `PlayerbotAIConfig` keeps a private `Config config;` and points it at aiplayerbot.conf with
// `config.SetSource(path)` (PlayerbotAIConfig.cpp:660-662), reads it with Get{String,Int,Float,Bool}Default (~700 calls),
// walks it with GetRootSections / GetKeys / GetValueHelper (PlayerbotAIConfig.cpp:70-85, :378) and reloads it with
// Reload (:290, :355). ForeverCore's ConfigMgr is a singleton for worldserver.conf with no SetSource, no per-file object
// and a private destructor, so it cannot be a member. The Turtle class (src/shared/Config/Config.h:30-78) wraps ACE's
// ACE_Configuration_Heap + ACE_Ini_ImpExp; ForeverCore has no ACE, so this class parses the same ini dialect itself.
// The rules were taken from ACE 7.1.2 by running its importer on a corpus of crafted files (see SLICE1-R3-NOTES.md) and
// are listed in TbCompat_Config.cpp, with the places where it differs.
//
// `sConfig` is NOT this class. On Turtle it is the main server config (mangosd.conf); the legacy code reads server keys
// from it (`sConfig.GetBoolDefault("TortoiseBots.AutoTest", false)`, `sConfig.GetFilename()`), which on ForeverCore is the
// core's ConfigMgr (worldserver.conf plus the module config); the shim keeps `sConfig` that way.
#pragma once

#include "Define.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ACE's string type: the legacy code only declares one and hands it to GetValueHelper.
using ACE_TString = std::string;

class TbLegacyConfig
{
public:
    TbLegacyConfig();
    ~TbLegacyConfig();
    TbLegacyConfig(TbLegacyConfig const&) = delete;
    TbLegacyConfig& operator=(TbLegacyConfig const&) = delete;

    // Remembers the file name, then reads it (Reload). false when the file cannot be opened or holds a line the ini
    // dialect rejects; the previous contents are dropped either way, as the Turtle class does.
    bool SetSource(char const* file);
    bool Reload();

    std::string GetStringDefault(char const* name, char const* def);
    bool GetBoolDefault(char const* name, bool def = false);
    int32 GetIntDefault(char const* name, int32 def);
    float GetFloatDefault(char const* name, float def);

    std::string GetFilename() const { return mFilename; }

    // First section that holds `name`; false when there is none or nothing is loaded.
    bool GetValueHelper(char const* name, ACE_TString& result);

    // For a profile config.
    std::string GetStringDefaultInSection(char const* name, char const* section, char const* def);
    float GetFloatDefault(char const* name, char const* section, float def);
    void GetRootSections(std::vector<std::string>& OutSectionList);
    void GetSections(char const* SectionName, std::vector<std::string>& OutSectionList);
    void GetKeys(char const* SectionName, std::vector<std::string>& OutKeysList);

private:
    struct Section;
    int Import(char const* file, std::unique_ptr<Section>& out) const;
    static Section* FindSection(Section* root, char const* path, bool create);

    std::string mFilename;
    std::unique_ptr<Section> mConf;
    std::mutex m_configLock;
};

// The name the legacy code uses (cmangos's class is `Config`).
using Config = TbLegacyConfig;
