// Feeds step rows to the loader's own checks (ai/playerbot/DungeonPlanPolicy.h): every row of a migration goes through
// ParsePlanStepRowKind, ParsePlanEntries and DungeonPlanTable::AddStep (which runs NormalizePlanStep and refuses a duplicate
// (plan, leg, seq)), as DungeonPlanMgr.cpp does at startup. tools/test_dungeon_plan_sql.py writes the rows and compiles this:
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_plan_rows.cpp -o /tmp/test_dungeon_plan_rows
// stdin, one record per line, fields separated by a tab:
//   P <plan> <map>
//   S <plan> <leg> <seq> <kind> <entry> <x> <y> <z> <radius> <item> <param> <flags> <timeout_s> <entries>
// stdout: "refused <plan> <leg> <seq> <kind> <reason>" for each refused row, then "rows <accepted> refused <refused>".
#include "../ai/playerbot/DungeonPlanPolicy.h"

#include <iostream>
#include <sstream>

using namespace ai;

static std::vector<std::string> Split(const std::string& line)
{
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t'))
        out.push_back(field);
    if (!line.empty() && line.back() == '\t')
        out.push_back("");
    return out;
}

int main()
{
    DungeonPlanTable table;
    unsigned accepted = 0, refused = 0;
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::vector<std::string> f = Split(line);
        if (f.size() == 3 && f[0] == "P")
        {
            table.AddPlan(f[1], static_cast<std::uint32_t>(std::stoul(f[2])));
            continue;
        }
        if (f.size() != 15 || f[0] != "S")
        {
            std::cout << "bad line: " << line << "\n";
            return 2;
        }
        PlanStep step;
        step.leg = static_cast<std::uint8_t>(std::stoul(f[2]));
        step.seq = static_cast<std::uint16_t>(std::stoul(f[3]));
        ParsePlanStepRowKind(f[4], step.kind, step.exactLaunch, step.openProof);
        step.entry = static_cast<std::uint32_t>(std::stoul(f[5]));
        step.x = std::stof(f[6]);
        step.y = std::stof(f[7]);
        step.z = std::stof(f[8]);
        step.radius = std::stof(f[9]);
        step.item = static_cast<std::uint32_t>(std::stoul(f[10]));
        step.param = static_cast<std::uint32_t>(std::stoul(f[11]));
        step.flags = static_cast<std::uint32_t>(std::stoul(f[12]));
        step.timeoutS = static_cast<std::uint16_t>(std::stoul(f[13]));
        PlanStepRefusal refusal = ParsePlanEntries(f[14], step.entries) ? table.AddStep(f[1], step) : PlanStepRefusal::BadEntries;
        if (refusal == PlanStepRefusal::None)
            ++accepted;
        else
        {
            ++refused;
            std::cout << "refused " << f[1] << " " << f[2] << " " << f[3] << " " << f[4] << " " << PlanStepRefusalName(refusal) << "\n";
        }
    }
    std::cout << "rows " << accepted << " refused " << refused << "\n";
    return refused ? 1 : 0;
}
