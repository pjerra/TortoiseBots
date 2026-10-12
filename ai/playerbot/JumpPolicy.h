#pragma once

namespace ai
{
    // Bots do not jump inside dungeons or raids (issue #125, owner decision
    // 2026-10-12). Instances have no navmesh link back down from raised
    // platforms (e.g. Uldaman Hall of the Keepers), so a jump that lands on
    // one strands the bot. `isDungeonOrRaid` is Map::IsDungeon(), which in
    // this core covers MAP_INSTANCE and MAP_RAID. Battlegrounds are not
    // included: the WSG roof/floor jumps there are scripted routes, and the
    // open world and BGs have no cut-off-island problem.
    //
    // Pure decision rule, no core includes. Used by JumpAction::isUseful
    // (which every jump strategy and the explicit `jump` command go through)
    // and by the follow path's "maybe jump is faster" shortcut check.
    inline bool JumpAllowedOnMap(bool isDungeonOrRaid)
    {
        return !isDungeonOrRaid;
    }
}
