// Piece P135-C (TBF #135): pure SQL builders and argument/action-line parsers for
// named AI presets (`ai_playerbot_db_store`) and custom strategies (`ai_playerbot_custom_strategy`).
//
// No core or legacy header may be included here: forever/tools/test_aipreset_pure.cpp compiles this file alone.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ai
{
namespace slice
{
namespace aipreset
{

inline std::string TrimCopy(std::string_view sv)
{
    std::size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start])))
        ++start;
    std::size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
        --end;
    return std::string(sv.substr(start, end - start));
}

inline std::string LowerCopy(std::string_view sv)
{
    std::string out;
    out.reserve(sv.size());
    for (char c : sv)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// --- Named presets (`ai_playerbot_db_store`) ---

inline std::string ListPresetsSql(uint32_t guid)
{
    return "SELECT DISTINCT `preset` FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " ORDER BY `preset`";
}

inline std::string SelectPresetValueRowsSql(uint32_t guid)
{
    return "SELECT `value` FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " AND `preset` = '' AND `key` = 'value'";
}

inline std::string DeleteDefaultValuesSql(uint32_t guid)
{
    return "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = " + std::to_string(guid) +
        " AND `key` = 'value'";
}

enum class AiOp
{
    Save,
    Load,
    List,
    Reset
};

struct ParsedAiArgs
{
    bool ok = false;
    AiOp op = AiOp::List;
    std::string botName;
    std::string preset;
    std::string errCode;
    std::string errMsg;
};

// Parses `<bot> save|load|list|reset [preset]` (used by both `.tb ai` and addon `ai`).
inline ParsedAiArgs ParseAiArgs(std::string_view raw)
{
    ParsedAiArgs out;
    std::string const trimmed = TrimCopy(raw);
    if (trimmed.empty())
    {
        out.errCode = "missing-argument";
        out.errMsg = "usage: ai <bot> save|load|list|reset [preset]";
        return out;
    }

    std::size_t const sp1 = trimmed.find(' ');
    if (sp1 == std::string::npos)
    {
        out.errCode = "missing-argument";
        out.errMsg = "usage: ai <bot> save|load|list|reset [preset]";
        return out;
    }
    out.botName = trimmed.substr(0, sp1);
    std::string const rest = TrimCopy(std::string_view(trimmed).substr(sp1 + 1));
    if (rest.empty())
    {
        out.errCode = "missing-argument";
        out.errMsg = "usage: ai <bot> save|load|list|reset [preset]";
        return out;
    }

    std::size_t const sp2 = rest.find(' ');
    std::string const opWord = LowerCopy(sp2 == std::string::npos ? std::string_view(rest) : std::string_view(rest).substr(0, sp2));
    std::string const tail = sp2 == std::string::npos ? std::string() : TrimCopy(std::string_view(rest).substr(sp2 + 1));

    if (opWord == "save")
        out.op = AiOp::Save;
    else if (opWord == "load")
        out.op = AiOp::Load;
    else if (opWord == "list")
        out.op = AiOp::List;
    else if (opWord == "reset")
        out.op = AiOp::Reset;
    else
    {
        out.errCode = "bad-argument";
        out.errMsg = "operation must be save, load, list, or reset";
        return out;
    }

    if ((out.op == AiOp::List || out.op == AiOp::Reset) && !tail.empty())
    {
        out.errCode = "bad-argument";
        out.errMsg = opWord + " takes no preset argument";
        return out;
    }

    if (tail.size() > 32)
    {
        out.errCode = "bad-argument";
        out.errMsg = "preset name must be at most 32 characters";
        return out;
    }

    out.preset = tail;
    out.ok = true;
    return out;
}

// --- Custom strategies (`ai_playerbot_custom_strategy`) ---

inline std::string SelectCustomLinesSql(std::string name, uint32_t owner, std::function<void(std::string&)> const& escape)
{
    escape(name);
    return "SELECT `action_line` FROM `ai_playerbot_custom_strategy` WHERE `name` = '" + name +
        "' AND `owner` = " + std::to_string(owner) + " ORDER BY `idx`";
}

inline std::string SelectCustomNamesSql(uint32_t owner)
{
    return "SELECT DISTINCT `name` FROM `ai_playerbot_custom_strategy` WHERE `owner` = " + std::to_string(owner) +
        " ORDER BY `name`";
}

inline std::string SelectCustomIndexedLinesSql(std::string name, uint32_t owner, std::function<void(std::string&)> const& escape)
{
    escape(name);
    return "SELECT `idx`, `action_line` FROM `ai_playerbot_custom_strategy` WHERE `name` = '" + name +
        "' AND `owner` = " + std::to_string(owner) + " ORDER BY `idx`";
}

inline std::string SelectCustomLineExistsSql(std::string name, uint32_t owner, uint32_t idx, std::function<void(std::string&)> const& escape)
{
    escape(name);
    return "SELECT `action_line` FROM `ai_playerbot_custom_strategy` WHERE `name` = '" + name +
        "' AND `owner` = " + std::to_string(owner) + " AND `idx` = " + std::to_string(idx);
}

inline std::string InsertCustomLineSql(std::string name, uint32_t owner, uint32_t idx, std::string line, std::function<void(std::string&)> const& escape)
{
    escape(name);
    escape(line);
    return "INSERT INTO `ai_playerbot_custom_strategy` (`name`, `owner`, `idx`, `action_line`) VALUES ('" +
        name + "', " + std::to_string(owner) + ", " + std::to_string(idx) + ", '" + line + "')";
}

inline std::string UpdateCustomLineSql(std::string name, uint32_t owner, uint32_t idx, std::string line, std::function<void(std::string&)> const& escape)
{
    escape(name);
    escape(line);
    return "UPDATE `ai_playerbot_custom_strategy` SET `action_line` = '" + line +
        "' WHERE `name` = '" + name + "' AND `owner` = " + std::to_string(owner) + " AND `idx` = " + std::to_string(idx);
}

inline std::string DeleteCustomLineSql(std::string name, uint32_t owner, uint32_t idx, std::function<void(std::string&)> const& escape)
{
    escape(name);
    return "DELETE FROM `ai_playerbot_custom_strategy` WHERE `name` = '" + name +
        "' AND `owner` = " + std::to_string(owner) + " AND `idx` = " + std::to_string(idx);
}

enum class CsOp
{
    Help,
    Print,
    Edit
};

struct ParsedCsArgs
{
    bool ok = false;
    CsOp op = CsOp::Help;
    std::string name;
    uint32_t idx = 0;
    std::string line;
    std::string errCode;
    std::string errMsg;
};

// Parses TB's `cs` grammar: `""` / `"?"` -> Help; `<name> ?` -> Print; `<name> <idx> [line]` -> Edit.
inline ParsedCsArgs ParseCsArgs(std::string_view raw)
{
    ParsedCsArgs out;
    std::string const trimmed = TrimCopy(raw);
    if (trimmed.empty() || trimmed == "?")
    {
        out.ok = true;
        out.op = CsOp::Help;
        return out;
    }

    std::size_t const sp1 = trimmed.find(' ');
    if (sp1 == std::string::npos)
    {
        out.ok = true;
        out.op = CsOp::Help;
        return out;
    }

    out.name = trimmed.substr(0, sp1);
    if (out.name.size() > 255)
    {
        out.errCode = "bad-argument";
        out.errMsg = "strategy name must be at most 255 characters";
        return out;
    }

    std::string const rest = TrimCopy(std::string_view(trimmed).substr(sp1 + 1));
    if (rest.empty())
    {
        out.errCode = "missing-argument";
        out.errMsg = "usage: cs <name> ? | cs <name> <idx> [action_line]";
        return out;
    }

    std::size_t const sp2 = rest.find(' ');
    std::string const idxWord = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
    if (idxWord == "?")
    {
        out.ok = true;
        out.op = CsOp::Print;
        return out;
    }

    for (char c : idxWord)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            out.errCode = "bad-argument";
            out.errMsg = "idx must be a non-negative integer or ?";
            return out;
        }
    }
    try
    {
        unsigned long long const parsed = std::stoull(idxWord);
        if (parsed > 0xFFFFFFFFULL)
        {
            out.errCode = "bad-argument";
            out.errMsg = "idx out of range";
            return out;
        }
        out.idx = static_cast<uint32_t>(parsed);
    }
    catch (...)
    {
        out.errCode = "bad-argument";
        out.errMsg = "idx out of range";
        return out;
    }

    out.line = sp2 == std::string::npos ? std::string() : TrimCopy(std::string_view(rest).substr(sp2 + 1));
    if (out.line.size() > 1024)
    {
        out.errCode = "bad-argument";
        out.errMsg = "action_line must be at most 1024 characters";
        return out;
    }

    out.ok = true;
    out.op = CsOp::Edit;
    return out;
}

// Splits a `CustomStrategy` action_line (`"<trigger>><actions>"`) and splits `<actions>` on top-level commas
// (ignoring commas inside `{...}`), matching `CustomStrategy.cpp`.
struct ParsedActionItem
{
    std::string action;
    std::string priority;
};

inline bool SplitCustomActionLine(std::string_view actionLine, std::string& trigger, std::vector<ParsedActionItem>& items)
{
    std::size_t const gt = actionLine.find('>');
    if (gt == std::string_view::npos || actionLine.find('>', gt + 1) != std::string_view::npos)
        return false;
    trigger = std::string(actionLine.substr(0, gt));
    if (trigger.empty())
        return false;

    std::string_view const actions = actionLine.substr(gt + 1);
    std::vector<std::string> tokens(1);
    int depth = 0;
    for (char a : actions)
    {
        if (a == '{')
            ++depth;
        else if (a == '}' && depth > 0)
            --depth;
        if (a == ',' && depth == 0)
            tokens.emplace_back();
        else
            tokens.back().push_back(a);
    }

    items.clear();
    for (std::string const& tok : tokens)
    {
        std::size_t const bang = tok.find('!');
        if (bang == std::string::npos)
        {
            if (tok.empty())
                return false;
            items.push_back({ tok, std::string() });
        }
        else
        {
            std::string act = tok.substr(0, bang);
            std::string prio = tok.substr(bang + 1);
            if (act.empty())
                return false;
            items.push_back({ std::move(act), std::move(prio) });
        }
    }
    return !items.empty();
}

} // namespace aipreset
} // namespace slice
} // namespace ai
