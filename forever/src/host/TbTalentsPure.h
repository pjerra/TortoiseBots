// Core-free talent build table and spec selection policy for ForeverCore bots.
// kBuildSteps is generated from forever/data/waterys/talent-builds.csv by forever/tools/talent_builds.py --write-header,
// which also checks every build against the class trait trees of client build 70959 (talent-trees-70959.csv).
// Pure header: no core or engine headers; unit-tested standalone by forever/tools/test_talents.cpp.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace talents
{

struct TalentStep
{
    uint8_t classId;
    uint8_t tab;
    uint32_t talentId;
    uint8_t ranks;
};

// A build step names a Talent.db2 ID, or for a talent without a Talent.db2 row (Forever-only talents) its TraitNode ID.
// Talent.db2 IDs stay far below this; trait node IDs start above it.
inline constexpr uint32_t kTraitNodeStepIdMin = 100000;

inline bool IsTraitNodeStepId(uint32_t id)
{
    return id >= kTraitNodeStepIdMin;
}

struct ClassWeights
{
    uint8_t classId;
    uint16_t w0;
    uint16_t w1;
    uint16_t w2;
};

inline constexpr ClassWeights kClassWeights[] = {
    {  1, 30, 30, 40 }, // Warrior: Arms, Fury, Protection
    {  2, 45, 30, 25 }, // Paladin: Holy, Protection, Retribution
    {  3, 40, 45, 15 }, // Hunter: Beast Mastery, Marksmanship, Survival
    {  4, 35, 50, 15 }, // Rogue: Assassination, Combat, Subtlety
    {  5, 30, 45, 25 }, // Priest: Discipline, Holy, Shadow
    {  7, 25, 25, 50 }, // Shaman: Elemental, Enhancement, Restoration
    {  8, 15, 35, 50 }, // Mage: Arcane, Fire, Frost
    {  9, 45, 20, 35 }, // Warlock: Affliction, Demonology, Destruction
    { 11, 20, 40, 40 }, // Druid: Balance, Feral, Restoration
};

inline constexpr TalentStep kBuildSteps[] = {
    {  1, 0,    130, 5 },
    {  1, 0,    127, 3 },
    {  1, 0,    126, 2 },
    {  1, 0,    641, 3 },
    {  1, 0,    131, 2 },
    {  1, 0,    137, 1 },
    {  1, 0,    121, 3 },
    {  1, 0,    136, 3 },
    {  1, 0,    662, 2 },
    {  1, 0,    133, 1 },
    {  1, 0, 105944, 5 },
    {  1, 0,    168, 2 },
    {  1, 0,    135, 1 },
    {  1, 0,    157, 5 },
    {  1, 0,    159, 5 },
    {  1, 0,    160, 1 },
    {  1, 0,    661, 3 },
    {  1, 0,    155, 4 },
    {  1, 1,    157, 5 },
    {  1, 1,    159, 5 },
    {  1, 1,    160, 1 },
    {  1, 1,    661, 3 },
    {  1, 1, 105953, 3 },
    {  1, 1,   1581, 5 },
    {  1, 1,   1542, 2 },
    {  1, 1,    165, 1 },
    {  1, 1,   1541, 2 },
    {  1, 1,    156, 3 },
    {  1, 1, 105977, 2 },
    {  1, 1,    167, 1 },
    {  1, 1,    155, 2 },
    {  1, 1,    124, 3 },
    {  1, 1,    127, 3 },
    {  1, 1,    641, 3 },
    {  1, 1,    131, 2 },
    {  1, 1,    137, 1 },
    {  1, 1,    121, 3 },
    {  1, 1,    130, 1 },
    {  1, 2,   1601, 5 },
    {  1, 2,    138, 5 },
    {  1, 2,    144, 3 },
    {  1, 2, 105971, 2 },
    {  1, 2,    146, 3 },
    {  1, 2, 105966, 1 },
    {  1, 2,    142, 1 },
    {  1, 2,    152, 1 },
    {  1, 2, 105961, 3 },
    {  1, 2,    142, 1 },
    {  1, 2,    702, 5 },
    {  1, 2,    148, 1 },
    {  1, 2,    147, 3 },
    {  1, 2,    153, 1 },
    {  1, 2,    128, 3 },
    {  1, 2,    149, 1 },
    {  1, 2,    150, 2 },
    {  1, 2,    124, 3 },
    {  1, 2,    130, 2 },
    {  1, 2,    641, 3 },
    {  1, 2,    126, 2 },
    {  2, 0,   1432, 5 },
    {  2, 0,   1444, 3 },
    {  2, 0, 110871, 2 },
    {  2, 0,   1433, 1 },
    {  2, 0,   1461, 5 },
    {  2, 0, 105704, 2 },
    {  2, 0, 110873, 2 },
    {  2, 0, 105330, 1 },
    {  2, 0, 105323, 1 },
    {  2, 0, 113631, 2 },
    {  2, 0, 105327, 2 },
    {  2, 0,   1627, 5 },
    {  2, 0, 105320, 1 },
    {  2, 0, 105324, 2 },
    {  2, 0, 110872, 1 },
    {  2, 0,   1449, 5 },
    {  2, 0,   1425, 2 },
    {  2, 0, 105632, 2 },
    {  2, 0,   1421, 1 },
    {  2, 0,   1423, 5 },
    {  2, 0,   1521, 1 },
    {  2, 1,   1421, 5 },
    {  2, 1,   1630, 3 },
    {  2, 1,   1425, 2 },
    {  2, 1, 105632, 2 },
    {  2, 1,   1424, 2 },
    {  2, 1,   1423, 5 },
    {  2, 1, 105625, 1 },
    {  2, 1,   1501, 3 },
    {  2, 1,   1629, 5 },
    {  2, 1,   1430, 1 },
    {  2, 1, 105639, 2 },
    {  2, 1, 110879, 3 },
    {  2, 1, 110875, 1 },
    {  2, 1,   1521, 3 },
    {  2, 1, 110878, 1 },
    {  2, 1,   1426, 2 },
    {  2, 1,   1407, 5 },
    {  2, 1,   1631, 2 },
    {  2, 1,   1403, 3 },
    {  2, 2,   1407, 5 },
    {  2, 2,   1631, 2 },
    {  2, 2,   1411, 3 },
    {  2, 2,   1481, 1 },
    {  2, 2,   1633, 3 },
    {  2, 2,   1634, 2 },
    {  2, 2,   1410, 3 },
    {  2, 2, 105700, 1 },
    {  2, 2,   1632, 2 },
    {  2, 2,   1441, 1 },
    {  2, 2,   1402, 3 },
    {  2, 2, 110882, 5 },
    {  2, 2, 105692, 1 },
    {  2, 2, 105701, 3 },
    {  2, 2,   1403, 1 },
    {  2, 2,   1450, 5 },
    {  2, 2,   1463, 5 },
    {  2, 2,   1449, 5 },
    {  3, 0,   1382, 5 },
    {  3, 0,   1389, 5 },
    {  3, 0, 104975, 2 },
    {  3, 0,   1391, 1 },
    {  3, 0,   1396, 5 },
    {  3, 0,   1393, 5 },
    {  3, 0,   1385, 2 },
    {  3, 0,   1387, 1 },
    {  3, 0,   1388, 2 },
    {  3, 0,   1390, 2 },
    {  3, 0,   1397, 5 },
    {  3, 0,   1386, 1 },
    {  3, 0,   1344, 5 },
    {  3, 0,   1342, 5 },
    {  3, 0,   1352, 3 },
    {  3, 0, 105005, 2 },
    {  3, 1,   1344, 5 },
    {  3, 1,   1342, 5 },
    {  3, 1, 105008, 5 },
    {  3, 1,   1346, 5 },
    {  3, 1, 105004, 1 },
    {  3, 1,   1349, 5 },
    {  3, 1,   1347, 3 },
    {  3, 1,   1353, 1 },
    {  3, 1,   1362, 5 },
    {  3, 1, 104997, 1 },
    {  3, 1,   1352, 3 },
    {  3, 1, 105005, 1 },
    {  3, 1,   1382, 5 },
    {  3, 1,   1389, 5 },
    {  3, 1, 104975, 1 },
    {  3, 2,   1311, 5 },
    {  3, 2,   1622, 5 },
    {  3, 2,   1621, 2 },
    {  3, 2,   1305, 3 },
    {  3, 2,   1310, 3 },
    {  3, 2,   1308, 1 },
    {  3, 2,   1306, 2 },
    {  3, 2,   1322, 2 },
    {  3, 2,   1312, 1 },
    {  3, 2, 110861, 5 },
    {  3, 2, 104985, 2 },
    {  3, 2, 110860, 2 },
    {  3, 2, 110859, 5 },
    {  3, 2, 104984, 1 },
    {  3, 2,   1304, 2 },
    {  3, 2,   1389, 5 },
    {  3, 2,   1382, 5 },
    {  4, 0,    270, 5 },
    {  4, 0,    273, 3 },
    {  4, 0,    277, 3 },
    {  4, 0,    269, 2 },
    {  4, 0,    382, 2 },
    {  4, 0,    682, 5 },
    {  4, 0,    280, 1 },
    {  4, 0,    268, 5 },
    {  4, 0, 105712, 1 },
    {  4, 0,    281, 5 },
    {  4, 0,    279, 2 },
    {  4, 0,    283, 3 },
    {  4, 0, 105709, 1 },
    {  4, 0,    272, 2 },
    {  4, 0,    203, 1 },
    {  4, 0,    201, 2 },
    {  4, 0,    276, 3 },
    {  4, 0,    181, 3 },
    {  4, 0,    187, 2 },
    {  4, 1,    186, 5 },
    {  4, 1,    181, 3 },
    {  4, 1,    187, 3 },
    {  4, 1,    301, 1 },
    {  4, 1,    204, 2 },
    {  4, 1,    222, 1 },
    {  4, 1,   1703, 2 },
    {  4, 1,   1122, 3 },
    {  4, 1, 108100, 1 },
    {  4, 1,    221, 2 },
    {  4, 1,    223, 1 },
    {  4, 1,    242, 5 },
    {  4, 1, 105740, 5 },
    {  4, 1,    205, 1 },
    {  4, 1,    276, 1 },
    {  4, 1,    270, 5 },
    {  4, 1,    273, 3 },
    {  4, 1,    277, 3 },
    {  4, 1,    269, 2 },
    {  4, 1,    382, 2 },
    {  4, 2,    261, 5 },
    {  4, 2,    263, 3 },
    {  4, 2,    265, 2 },
    {  4, 2,    381, 1 },
    {  4, 2,   1123, 3 },
    {  4, 2,    245, 1 },
    {  4, 2,    247, 2 },
    {  4, 2,    303, 1 },
    {  4, 2,    246, 2 },
    {  4, 2,    284, 1 },
    {  4, 2,    681, 1 },
    {  4, 2,   1701, 2 },
    {  4, 2,    244, 1 },
    {  4, 2, 105750, 5 },
    {  4, 2, 110866, 1 },
    {  4, 2,    241, 3 },
    {  4, 2, 105757, 2 },
    {  4, 2,    270, 5 },
    {  4, 2,    273, 3 },
    {  4, 2,    277, 3 },
    {  4, 2,    269, 2 },
    {  4, 2,    382, 2 },
    {  5, 0, 105849, 5 },
    {  5, 0,    352, 3 },
    {  5, 0,   1201, 5 },
    {  5, 0,    348, 1 },
    {  5, 0,    343, 3 },
    {  5, 0,    347, 3 },
    {  5, 0,    341, 3 },
    {  5, 0,    346, 3 },
    {  5, 0, 105839, 1 },
    {  5, 0, 105834, 1 },
    {  5, 0, 105835, 5 },
    {  5, 0, 105840, 3 },
    {  5, 0,    322, 1 },
    {  5, 0, 110853, 3 },
    {  5, 0,    406, 3 },
    {  5, 0,    410, 2 },
    {  5, 0,   1181, 5 },
    {  5, 0, 110855, 1 },
    {  5, 1,    406, 3 },
    {  5, 1, 110855, 2 },
    {  5, 1,   1181, 5 },
    {  5, 1,    361, 3 },
    {  5, 1,    442, 1 },
    {  5, 1,   1636, 1 },
    {  5, 1,   1635, 2 },
    {  5, 1,    408, 3 },
    {  5, 1,   1561, 1 },
    {  5, 1,    402, 5 },
    {  5, 1, 105855, 2 },
    {  5, 1,    404, 3 },
    {  5, 1, 105851, 1 },
    {  5, 1,    410, 3 },
    {  5, 1, 110855, 3 },
    {  5, 1,    345, 2 },
    {  5, 1, 105849, 3 },
    {  5, 1,    321, 2 },
    {  5, 1,   1201, 3 },
    {  5, 1,    341, 2 },
    {  5, 1,    348, 1 },
    {  5, 2,    463, 5 },
    {  5, 2,    481, 5 },
    {  5, 2,    501, 1 },
    {  5, 2, 105825, 2 },
    {  5, 2,    482, 2 },
    {  5, 2,    484, 1 },
    {  5, 2,    461, 3 },
    {  5, 2, 105823, 3 },
    {  5, 2,    541, 1 },
    {  5, 2, 105819, 2 },
    {  5, 2,    462, 5 },
    {  5, 2, 110854, 2 },
    {  5, 2,    521, 1 },
    {  5, 2,    881, 2 },
    {  5, 2,    465, 1 },
    {  5, 2, 105849, 5 },
    {  5, 2,   1201, 5 },
    {  5, 2,    341, 3 },
    {  5, 2,    348, 1 },
    {  5, 2,    347, 1 },
    {  8, 0,     78, 2 },
    {  8, 0,     85, 3 },
    {  8, 0,     74, 2 },
    {  8, 0,     75, 5 },
    {  8, 0, 105809, 2 },
    {  8, 0,     81, 3 },
    {  8, 0, 105806, 1 },
    {  8, 0,   1142, 3 },
    {  8, 0, 113627, 1 },
    {  8, 0,     86, 1 },
    {  8, 0,     77, 5 },
    {  8, 0,    421, 3 },
    {  8, 0,     87, 1 },
    {  8, 0,     80, 5 },
    {  8, 0,   1650, 2 },
    {  8, 0,     82, 1 },
    {  8, 0,   1649, 3 },
    {  8, 0,     37, 2 },
    {  8, 0,     73, 5 },
    {  8, 0,     38, 1 },
    {  8, 1,   1141, 5 },
    {  8, 1,     34, 5 },
    {  8, 1,     28, 2 },
    {  8, 1,     29, 1 },
    {  8, 1,     27, 2 },
    {  8, 1,     23, 2 },
    {  8, 1,     25, 3 },
    {  8, 1,   1639, 3 },
    {  8, 1, 105786, 1 },
    {  8, 1,     33, 3 },
    {  8, 1,     32, 1 },
    {  8, 1,     35, 3 },
    {  8, 1,     36, 1 },
    {  8, 1,     26, 5 },
    {  8, 1,     30, 1 },
    {  8, 1,     78, 2 },
    {  8, 1,     85, 3 },
    {  8, 1,     75, 5 },
    {  8, 1,     74, 2 },
    {  8, 1,   1650, 1 },
    {  8, 2,     37, 5 },
    {  8, 2,     73, 5 },
    {  8, 2,     62, 2 },
    {  8, 2,     66, 3 },
    {  8, 2, 105767, 1 },
    {  8, 2,    741, 2 },
    {  8, 2,     72, 1 },
    {  8, 2,     67, 3 },
    {  8, 2,     64, 3 },
    {  8, 2,     69, 1 },
    {  8, 2, 105764, 2 },
    {  8, 2,     68, 5 },
    {  8, 2,     71, 1 },
    {  8, 2,   1649, 3 },
    {  8, 2,     38, 1 },
    {  8, 2,     78, 2 },
    {  8, 2,     85, 3 },
    {  8, 2,     75, 5 },
    {  8, 2,     74, 2 },
    {  8, 2,   1650, 1 },
    {  7, 0,    564, 5 },
    {  7, 0,    563, 5 },
    {  7, 0,    721, 3 },
    {  7, 0,    561, 3 },
    {  7, 0,    575, 5 },
    {  7, 0,    574, 1 },
    {  7, 0,   1642, 3 },
    {  7, 0,    562, 1 },
    {  7, 0, 104759, 3 },
    {  7, 0,   1641, 2 },
    {  7, 0,    565, 5 },
    {  7, 0,    573, 1 },
    {  7, 0,    567, 2 },
    {  7, 0,   1640, 2 },
    {  7, 0,    586, 5 },
    {  7, 0,    593, 5 },
    {  7, 1,    613, 5 },
    {  7, 1,    614, 5 },
    {  7, 1, 104755, 3 },
    {  7, 1,    607, 3 },
    {  7, 1,    611, 3 },
    {  7, 1, 104749, 1 },
    {  7, 1,    601, 3 },
    {  7, 1,    602, 5 },
    {  7, 1,    901, 1 },
    {  7, 1,    616, 1 },
    {  7, 1, 104744, 2 },
    {  7, 1, 104742, 2 },
    {  7, 1, 104741, 5 },
    {  7, 1, 104740, 1 },
    {  7, 1,    563, 5 },
    {  7, 1,   1645, 3 },
    {  7, 1,    561, 3 },
    {  7, 2,    586, 5 },
    {  7, 2,    593, 5 },
    {  7, 2, 104734, 3 },
    {  7, 2,    581, 3 },
    {  7, 2,    587, 3 },
    {  7, 2, 104732, 1 },
    {  7, 2,    594, 5 },
    {  7, 2,    588, 5 },
    {  7, 2,    590, 1 },
    {  7, 2,   1648, 3 },
    {  7, 2,    591, 1 },
    {  7, 2,    592, 5 },
    {  7, 2, 104724, 1 },
    {  7, 2,    614, 5 },
    {  7, 2,    609, 2 },
    {  7, 2, 104755, 3 },
    {  9, 0,   1003, 5 },
    {  9, 0,   1005, 5 },
    {  9, 0, 105920, 3 },
    {  9, 0,   1021, 2 },
    {  9, 0,   1284, 2 },
    {  9, 0,   1001, 3 },
    {  9, 0,   1061, 1 },
    {  9, 0, 105917, 3 },
    {  9, 0, 110876, 5 },
    {  9, 0,   1002, 2 },
    {  9, 0,   1041, 1 },
    {  9, 0,   1004, 3 },
    {  9, 0,   1042, 5 },
    {  9, 0, 105909, 1 },
    {  9, 0,    943, 5 },
    {  9, 0,    941, 3 },
    {  9, 0,    964, 2 },
    {  9, 1,   1222, 3 },
    {  9, 1,   1223, 5 },
    {  9, 1,   1242, 3 },
    {  9, 1, 105902, 2 },
    {  9, 1,   1281, 1 },
    {  9, 1,   1227, 2 },
    {  9, 1,   1226, 1 },
    {  9, 1, 105896, 3 },
    {  9, 1, 105897, 2 },
    {  9, 1,   1282, 1 },
    {  9, 1, 105893, 3 },
    {  9, 1,   1244, 5 },
    {  9, 1, 105890, 1 },
    {  9, 1,   1262, 5 },
    {  9, 1,   1224, 2 },
    {  9, 1,   1225, 3 },
    {  9, 1, 105899, 2 },
    {  9, 1,   1007, 2 },
    {  9, 1,   1003, 5 },
    {  9, 2,    944, 5 },
    {  9, 2,    943, 5 },
    {  9, 2,    941, 3 },
    {  9, 2,    967, 5 },
    {  9, 2,    963, 1 },
    {  9, 2,    985, 3 },
    {  9, 2, 105880, 1 },
    {  9, 2,    986, 2 },
    {  9, 2, 105876, 1 },
    {  9, 2, 105877, 3 },
    {  9, 2, 105875, 5 },
    {  9, 2, 105874, 1 },
    {  9, 2,    964, 2 },
    {  9, 2,    982, 5 },
    {  9, 2,    965, 2 },
    {  9, 2,   1007, 2 },
    {  9, 2,   1003, 5 },
    { 11, 0,    762, 5 },
    { 11, 0,    783, 3 },
    { 11, 0,    763, 2 },
    { 11, 0,    764, 2 },
    { 11, 0, 104927, 2 },
    { 11, 0, 104928, 1 },
    { 11, 0,    792, 5 },
    { 11, 0,    784, 5 },
    { 11, 0,    827, 1 },
    { 11, 0,    789, 1 },
    { 11, 0, 104935, 3 },
    { 11, 0,    790, 5 },
    { 11, 0,    793, 1 },
    { 11, 0, 104924, 5 },
    { 11, 0,    823, 5 },
    { 11, 0,    829, 3 },
    { 11, 0,    824, 2 },
    { 11, 1,    796, 5 },
    { 11, 1,    807, 2 },
    { 11, 1,    799, 3 },
    { 11, 1,    794, 3 },
    { 11, 1,    805, 2 },
    { 11, 1,    798, 2 },
    { 11, 1, 104944, 1 },
    { 11, 1,    803, 3 },
    { 11, 1,    801, 2 },
    { 11, 1, 104949, 1 },
    { 11, 1,    809, 1 },
    { 11, 1, 104950, 2 },
    { 11, 1, 104953, 5 },
    { 11, 1, 104956, 1 },
    { 11, 1,    822, 5 },
    { 11, 1,    797, 2 },
    { 11, 1,    802, 1 },
    { 11, 1,    808, 5 },
    { 11, 1,    781, 3 },
    { 11, 1,    824, 2 },
    { 11, 2,    823, 5 },
    { 11, 2,    824, 5 },
    { 11, 2,    829, 3 },
    { 11, 2,    828, 5 },
    { 11, 2,    841, 3 },
    { 11, 2, 104918, 1 },
    { 11, 2,    843, 5 },
    { 11, 2,    830, 3 },
    { 11, 2,    844, 1 },
    { 11, 2,    831, 1 },
    { 11, 2, 104911, 3 },
    { 11, 2,    825, 5 },
    { 11, 2, 104910, 1 },
    { 11, 2,    762, 5 },
    { 11, 2,    763, 2 },
    { 11, 2,    783, 3 },
};

inline char const* SpecName(uint8_t classId, int tab)
{
    if (tab < 0 || tab > 2)
        return "none";
    switch (classId)
    {
        case 1:  return tab == 0 ? "arms" : (tab == 1 ? "fury" : "protection");
        case 2:  return tab == 0 ? "holy" : (tab == 1 ? "protection" : "retribution");
        case 3:  return tab == 0 ? "beastmastery" : (tab == 1 ? "marksmanship" : "survival");
        case 4:  return tab == 0 ? "assassination" : (tab == 1 ? "combat" : "subtlety");
        case 5:  return tab == 0 ? "discipline" : (tab == 1 ? "holy" : "shadow");
        case 7:  return tab == 0 ? "elemental" : (tab == 1 ? "enhancement" : "restoration");
        case 8:  return tab == 0 ? "arcane" : (tab == 1 ? "fire" : "frost");
        case 9:  return tab == 0 ? "affliction" : (tab == 1 ? "demonology" : "destruction");
        case 11: return tab == 0 ? "balance" : (tab == 1 ? "feral" : "restoration");
        default: return "unknown";
    }
}

// The pool's spec roll with the live server's weights (TortoiseBots.Pool.CensusSpec): the share of each spec among
// a class's level 60 characters, per mille, copied from forever/data/aotc/spec-population.csv (aotc.gg, read 2026-11-24).
// Tab order is the class's own (SpecName): the same tabs kClassWeights above comment.
inline constexpr ClassWeights kCensusSpecWeights[] = {
    {  1, 100, 310, 580 }, // Warrior: Arms, Fury, Protection
    {  2, 100, 400, 500 }, // Paladin: Holy, Protection, Retribution
    {  3, 610, 260, 130 }, // Hunter: Beast Mastery, Marksmanship, Survival
    {  4,  90, 720, 190 }, // Rogue: Assassination, Combat, Subtlety
    {  5, 160, 250, 590 }, // Priest: Discipline, Holy, Shadow
    {  7, 120, 690, 190 }, // Shaman: Elemental, Enhancement, Restoration
    {  8, 350,  40, 610 }, // Mage: Arcane, Fire, Frost
    {  9, 600, 350,  50 }, // Warlock: Affliction, Demonology, Destruction
    { 11, 160, 630, 220 }, // Druid: Balance, Feral, Restoration
};

inline int PickSpecTabForBot(uint8_t classId, uint32_t guidLow, int forcedSpecTab, int tab0Spent, int tab1Spent, int tab2Spent,
    bool censusWeights = false)
{
    if (forcedSpecTab >= 0 && forcedSpecTab <= 2)
        return forcedSpecTab;

    if (tab0Spent + tab1Spent + tab2Spent > 0)
    {
        int const counts[3] = { tab0Spent, tab1Spent, tab2Spent };
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (counts[i] > counts[best])
                best = i;
        return best;
    }

    uint32_t h = guidLow * 0x9E3779B1u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;

    for (ClassWeights const& cw : censusWeights ? kCensusSpecWeights : kClassWeights)
    {
        if (cw.classId != classId)
            continue;
        uint32_t const total = uint32_t(cw.w0) + cw.w1 + cw.w2;
        if (!total)
            return 0;
        uint32_t const roll = h % total;
        if (roll < cw.w0)
            return 0;
        if (roll < uint32_t(cw.w0) + cw.w1)
            return 1;
        return 2;
    }
    return 0;
}

// The census's share of one spec tab among its class's level 60 characters, per mille (0 for a class outside the table).
inline uint16_t CensusSpecSharePm(uint8_t classId, int tab)
{
    if (tab < 0 || tab > 2)
        return 0;
    for (ClassWeights const& cw : kCensusSpecWeights)
    {
        if (cw.classId != classId)
            continue;
        return tab == 0 ? cw.w0 : (tab == 1 ? cw.w1 : cw.w2);
    }
    return 0;
}

inline bool TalentMatchesClass(int8_t talentClassId, uint8_t targetClassId)
{
    // Classic 1.60.1 Talent.db2 leaves ClassID=0 and scopes via TabID; accept 0 or exact match.
    return talentClassId == 0 || talentClassId == int8_t(targetClassId);
}

inline size_t BuildStepCount(uint8_t classId, int tab)
{
    size_t n = 0;
    for (TalentStep const& s : kBuildSteps)
        if (s.classId == classId && int(s.tab) == tab)
            ++n;
    return n;
}

inline std::vector<uint32_t> PlanNextSteps(uint8_t classId, int tab, std::unordered_map<uint32_t, uint8_t> const& currentRanks)
{
    std::vector<uint32_t> out;
    if (tab < 0 || tab > 2)
        return out;

    std::unordered_map<uint32_t, uint8_t> current = currentRanks;
    std::unordered_map<uint32_t, uint8_t> cumulative;
    for (TalentStep const& step : kBuildSteps)
    {
        if (step.classId != classId || int(step.tab) != tab)
            continue;
        uint8_t& target = cumulative[step.talentId];
        target = uint8_t(target + step.ranks);
        uint8_t& cur = current[step.talentId];
        while (cur < target)
        {
            out.push_back(step.talentId);
            ++cur;
        }
    }
    return out;
}

struct FallbackNode
{
    int32_t traitNodeId = 0;
    int32_t traitNodeEntryId = 0;
    int32_t maxRanks = 0;
    int32_t posX = 0;
    int32_t posY = 0;
    int tab = -1;
};

// Orders valid talent nodes for fallback spending: mainTab first, then remaining tabs (0..2),
// and within each tab by row (posY ascending, top to bottom), then left-to-right (posX ascending).
inline std::vector<FallbackNode> OrderFallbackNodes(std::vector<FallbackNode> nodes, int mainTab)
{
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](FallbackNode const& n)
    {
        return n.tab < 0 || n.tab > 2 || n.maxRanks <= 0 || n.traitNodeId <= 0 || n.traitNodeEntryId <= 0;
    }), nodes.end());

    auto tabRank = [mainTab](int tab) -> int
    {
        return tab == mainTab ? 0 : (1 + tab);
    };

    std::sort(nodes.begin(), nodes.end(), [&](FallbackNode const& a, FallbackNode const& b)
    {
        int const ta = tabRank(a.tab);
        int const tb = tabRank(b.tab);
        if (ta != tb)
            return ta < tb;
        if (a.posY != b.posY)
            return a.posY < b.posY;
        if (a.posX != b.posX)
            return a.posX < b.posX;
        if (a.traitNodeId != b.traitNodeId)
            return a.traitNodeId < b.traitNodeId;
        return a.traitNodeEntryId < b.traitNodeEntryId;
    });
    return nodes;
}

} // namespace talents
} // namespace TortoiseBots
