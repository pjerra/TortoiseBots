// The ini reader behind `Config` (TbCompat_Config.h). The rules are those of ACE 7.1.2's ACE_Ini_ImpExp::import_config
// feeding an ACE_Configuration_Heap, which is what the Turtle class (src/shared/Config/Config.cpp) uses. Each rule below
// was observed by running ACE's importer on a crafted file and compared with this parser on the same files (the repository
// has no unit-test place for forever/ code, so the harness is described in forever/docs/SLICE1-R3-NOTES.md, not shipped).
//
//   file        opened with fopen; a file that cannot be opened is rc -1. An empty file, or one with only comments, is ok.
//   lines       read in chunks of at most 4095 characters (a longer line arrives as several lines: the first part is read as
//               a line, the rest as the next); leading and trailing whitespace is dropped; a blank line, or one whose first
//               character is ';' or '#', is skipped. A '#' or ';' later in a line is NOT a comment (it stays in the value).
//   section     a line starting with '[' ends at its LAST ']'; the text between is the section name (not trimmed: "[ A ]"
//               is the section " A "), anything after the last ']' is ignored. A name that holds '[' or ']', or a line
//               with '[' and no ']', is rc -3. '\' and '/' in a name nest sections (A\B is B inside A). An empty name "[]"
//               keeps the current section. Section names are case-insensitive; a section named again is the same section.
//   key = value a line with no '=' is rc -3. The key is the text before the FIRST '=', trimmed; the value the text after it,
//               trimmed; when the value then starts and ends with '"' both quotes are removed ("a b" -> a b, a lone '"' ->
//               empty); single quotes are ordinary characters. A key before any section, or a key holding '\', '[' or ']',
//               is rc -4. Keys are case-insensitive; a key set again keeps its first spelling and takes the new value.
//   errors      the import stops at the first bad line with what was read so far in the store, and the Turtle class's Reload
//               then throws that store away: after a failed SetSource every lookup returns its default.
//
// Differences from ACE, on purpose:
//   * Enumeration order. ACE hands sections and keys back in its hash order, which is not meaningful; this class keeps the
//     order the file introduced them. A key present in several sections is found in the first section that introduced it
//     (ACE: the first in its hash order). aiplayerbot.conf has one section, so no real file is affected.
//   * ACE refuses a section header written in capitals that matches an earlier, differently-cased section ("[Conf]" then
//     "[CONF]" is rc -3 in ACE 7.1.2, while "[COnf]" is accepted as the same section); here both are accepted.
//   * GetRootSections / GetKeys / GetSections on a store that holds nothing return an empty list (the Turtle class
//     dereferences a null pointer).
#include "TbCompat_Config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace
{
    std::string Lower(std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

    std::string Trim(std::string const& text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && IsSpace(text[begin]))
            ++begin;
        while (end > begin && IsSpace(text[end - 1]))
            --end;
        return text.substr(begin, end - begin);
    }
}

struct TbLegacyConfig::Section
{
    struct Value
    {
        std::string key;     // first spelling
        std::string value;
    };

    std::string name;                                   // first spelling
    std::vector<Value> values;                          // in file order
    std::map<std::string, size_t> valueIndex;           // lower-case key -> values[]
    std::vector<std::unique_ptr<Section>> children;     // in file order
    std::map<std::string, size_t> childIndex;           // lower-case name -> children[]

    Section* Child(std::string const& childName, bool create)
    {
        auto itr = childIndex.find(Lower(childName));
        if (itr != childIndex.end())
            return children[itr->second].get();
        if (!create)
            return nullptr;
        children.push_back(std::make_unique<Section>());
        children.back()->name = childName;
        childIndex[Lower(childName)] = children.size() - 1;
        return children.back().get();
    }

    Value const* Find(std::string const& key) const
    {
        auto itr = valueIndex.find(Lower(key));
        return itr == valueIndex.end() ? nullptr : &values[itr->second];
    }

    void Set(std::string const& key, std::string const& value)
    {
        auto itr = valueIndex.find(Lower(key));
        if (itr != valueIndex.end())
        {
            values[itr->second].value = value;
            return;
        }
        values.push_back({ key, value });
        valueIndex[Lower(key)] = values.size() - 1;
    }
};

TbLegacyConfig::TbLegacyConfig() = default;
TbLegacyConfig::~TbLegacyConfig() = default;

// A section path: '\' and '/' separate the levels; an empty level is skipped. `root` is the unnamed top.
TbLegacyConfig::Section* TbLegacyConfig::FindSection(Section* root, char const* path, bool create)
{
    Section* current = root;
    std::string level;
    auto step = [&]() -> bool {
        if (level.empty())
            return true;
        current = current->Child(level, create);
        level.clear();
        return current != nullptr;
    };
    for (char const* c = path; *c; ++c)
    {
        if (*c == '\\' || *c == '/')
        {
            if (!step())
                return nullptr;
        }
        else
            level += *c;
    }
    return step() ? current : nullptr;
}

// Reads `file` into a fresh tree. Returns ACE's code; on a non-zero code `out` holds what was read before the bad line.
int TbLegacyConfig::Import(char const* file, std::unique_ptr<Section>& out) const
{
    out = std::make_unique<Section>();
    FILE* in = std::fopen(file, "r");
    if (!in)
        return -1;

    Section* current = nullptr;            // nullptr = the root, which holds no values
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, in))
    {
        std::string const line = Trim(buffer);   // (a NUL inside a chunk ends it, as in C)
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[')
        {
            size_t const end = line.rfind(']');
            if (end == std::string::npos)
            {
                std::fclose(in);
                return -3;
            }
            std::string const name = line.substr(1, end - 1);
            if (name.find_first_of("[]") != std::string::npos)
            {
                std::fclose(in);
                return -3;
            }
            if (!name.empty())   // an empty name keeps the current section
            {
                Section* found = FindSection(out.get(), name.c_str(), true);
                if (found != out.get())   // a name of only separators is the root again, which holds no values
                    current = found;
                else
                    current = nullptr;
            }
            continue;
        }

        size_t const eq = line.find('=');
        if (eq == std::string::npos)
        {
            std::fclose(in);
            return -3;
        }
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        if (value.size() >= 1 && value.front() == '"' && value.back() == '"')
            value = value.size() == 1 ? std::string() : value.substr(1, value.size() - 2);

        if (!current || key.find_first_of("\\[]") != std::string::npos)
        {
            std::fclose(in);
            return -4;
        }
        current->Set(key, value);
    }
    std::fclose(in);
    return 0;
}

bool TbLegacyConfig::SetSource(char const* file)
{
    mFilename = file;
    return Reload();
}

bool TbLegacyConfig::Reload()
{
    std::lock_guard<std::mutex> guard(m_configLock);
    std::unique_ptr<Section> fresh;
    if (Import(mFilename.c_str(), fresh) == 0)
    {
        mConf = std::move(fresh);
        return true;
    }
    mConf.reset();
    return false;
}

bool TbLegacyConfig::GetValueHelper(char const* name, ACE_TString& result)
{
    std::lock_guard<std::mutex> guard(m_configLock);
    if (!mConf)
        return false;
    for (auto const& section : mConf->children)
    {
        if (Section::Value const* value = section->Find(name))
        {
            result = value->value;
            return true;
        }
    }
    return false;
}

std::string TbLegacyConfig::GetStringDefaultInSection(char const* name, char const* section, char const* def)
{
    std::lock_guard<std::mutex> guard(m_configLock);
    if (!mConf)
        return def;
    Section* found = FindSection(mConf.get(), section, false);
    if (!found || found == mConf.get())
        return def;
    Section::Value const* value = found->Find(name);
    return value ? value->value : std::string(def);
}

std::string TbLegacyConfig::GetStringDefault(char const* name, char const* def)
{
    ACE_TString val;
    return GetValueHelper(name, val) ? val : std::string(def);
}

bool TbLegacyConfig::GetBoolDefault(char const* name, bool def)
{
    ACE_TString val;
    if (!GetValueHelper(name, val))
        return def;

    char const* str = val.c_str();
    return std::strcmp(str, "true") == 0 || std::strcmp(str, "TRUE") == 0 ||
           std::strcmp(str, "yes") == 0 || std::strcmp(str, "YES") == 0 ||
           std::strcmp(str, "1") == 0;
}

int32 TbLegacyConfig::GetIntDefault(char const* name, int32 def)
{
    ACE_TString val;
    return GetValueHelper(name, val) ? std::atoi(val.c_str()) : def;
}

float TbLegacyConfig::GetFloatDefault(char const* name, float def)
{
    ACE_TString val;
    return GetValueHelper(name, val) ? static_cast<float>(std::atof(val.c_str())) : def;
}

float TbLegacyConfig::GetFloatDefault(char const* name, char const* section, float def)
{
    std::string rawValue = GetStringDefaultInSection(name, section, "invalid");
    if (rawValue == "invalid")
        return def;
    return static_cast<float>(std::atof(rawValue.c_str()));
}

void TbLegacyConfig::GetRootSections(std::vector<std::string>& OutSectionList)
{
    std::lock_guard<std::mutex> guard(m_configLock);
    if (!mConf)
        return;
    for (auto const& section : mConf->children)
        OutSectionList.emplace_back(section->name);
}

void TbLegacyConfig::GetSections(char const* SectionName, std::vector<std::string>& OutSectionList)
{
    std::lock_guard<std::mutex> guard(m_configLock);
    if (!mConf)
        return;
    Section* base = FindSection(mConf.get(), SectionName, false);
    if (!base || base == mConf.get())
        return;
    for (auto const& section : base->children)
        OutSectionList.emplace_back(section->name);
}

void TbLegacyConfig::GetKeys(char const* SectionName, std::vector<std::string>& OutKeysList)
{
    std::lock_guard<std::mutex> guard(m_configLock);
    if (!mConf)
        return;
    Section* base = FindSection(mConf.get(), SectionName, false);
    if (!base || base == mConf.get())
        return;
    for (auto const& value : base->values)
        OutKeysList.emplace_back(value.key);
}
