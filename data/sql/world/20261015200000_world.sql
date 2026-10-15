-- Dungeon plans for the run leader (.bot run): plan-wide unit lists on `ai_dungeon_plan` (issue #999).
--
--   cc_priority  creature entries the run leader crowd-controls in a pack, best first (`cc moon <guid> <mage>`
--                before the pull); plan v1's cc_priority
--   cc_always    entries crowd-controlled in any pack they are in (ranked after cc_priority); plan v1's cc_always
--   kill_first   entries the party kills first in a fight (`focus <guid>`; a summoned healing ward too); plan v1's
--                kill_first
-- Each is a comma list as in `ai_dungeon_step`.`entries` ('' none). Read once at startup
-- (ai/playerbot/DungeonPlanMgr.cpp).
--
-- Idempotent: ADD COLUMN IF NOT EXISTS only. Never edit this file once applied; add a newer migration.

ALTER TABLE `ai_dungeon_plan`
  ADD COLUMN IF NOT EXISTS `cc_priority` varchar(255) NOT NULL DEFAULT '' AFTER `map`,
  ADD COLUMN IF NOT EXISTS `cc_always` varchar(255) NOT NULL DEFAULT '' AFTER `cc_priority`,
  ADD COLUMN IF NOT EXISTS `kill_first` varchar(255) NOT NULL DEFAULT '' AFTER `cc_always`;
