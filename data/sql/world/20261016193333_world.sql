-- Issues #959 and #970: gear weight scale follow-ups.
--
-- #959: fire (24) and frost (25) mages weight off-school arcsplpwr at 7,
--   equal to their own school and the generic splpwr, so a pure-Arcane item
--   scores exactly like a pure-Fire (or pure-Frost) one and the bot wears
--   whichever it finds. Arcane Explosion is in every mage kit, so a non-zero
--   weight is right, but only on packs: lower arcsplpwr to 2 so the own
--   school always wins while Arcane still beats an item with no spell damage
--   at all. Arcane (23) keeps arcsplpwr 7 (own school). Warlock schools are
--   not the same bug: every warlock spec (26, 27, 28) weights firsplpwr and
--   shasplpwr equally at 5, and each spec casts both schools (affliction and
--   demonology lean shadow, destruction leans fire, but none is foreign), so
--   no school is zeroed or lowered. Shadow priest (15) only weights shasplpwr
--   and never the other schools; elemental shaman (20) weights natsplpwr
--   alongside splpwr, which is its own school; balance druid (29) weights
--   arcsplpwr 11, but Starfire is Arcane, so that is its own school too.
--   No other class shows the same-bug pattern.
--
-- #970: only tank scales weight sta, so a strictly-better item (same primary
--   stat plus more Stamina) ties the equipped piece and loses to the 1.1
--   EquipUpgradeThreshold. The earlier #219 migration gave sta to warriors
--   (1, 2: 15), healers (4: 12, 13: 10, 14: 12, 22: 10, 31: 10) and warlocks
--   (26, 27, 28: 6), and tanks already have it (3: 18, 5: 15, 30: 16), so
--   only the pure-DPS scales still lack it: hunters (7, 8, 9), rogues
--   (10, 11, 12), retribution (6), enhancement (21), casters
--   (15, 20, 23, 24, 25, 29) and feral dps (32). Give each of those sta = 1:
--   well below every primary stat (agi/str 2-11, atkpwr 1-35, splpwr 5-29,
--   school power 5-11), so it only breaks ties without outweighing damage
--   stats -- the same shape as the donor mod-playerbots, which weights sta
--   only on its tank specs and leaves DPS/healer scales at 0, except here a
--   tie-break 1 instead of 0. Donor reference (read-only):
--   ../playerbots-references/mod-playerbots/data/sql/playerbots/base/
--   playerbots_weightscale_data.sql (tanks sta 75-100, all other specs 0).
--   Note the threshold math: the 1.1 factor needs the gain to exceed 10% of
--   the old score, so sta = 1 fixes small items but not big ones. The
--   reporter's belt goes 33 vs 29 (33 > 29 * 1.1 = 31.9, wins), while a +4
--   Stamina gain on a 200-power epic scores 204 vs 200 and still loses
--   (204 < 200 * 1.1 = 220). Exact ties still fall through to the
--   sheet/quality/item-level tiebreaks below the threshold. A weight change
--   alone therefore cannot make every strictly-better item win; the minimal
--   code fix would be a dominance check in ItemUsageValue (new item >= old
--   in every stat and better in at least one bypasses the threshold), which
--   this migration deliberately does not add.
--
-- Idempotency: DELETEs the touched (id, field) rows before re-inserting
-- (ai_playerbot_weightscale_data has no unique key and a duplicate stat row
-- would double-count a weight). The core runs each migration file inside one
-- transaction, so this file has no START TRANSACTION.

DELETE FROM `ai_playerbot_weightscale_data`
WHERE (`id` IN (24,25) AND `field` = 'arcsplpwr')
   OR (`id` IN (6,7,8,9,10,11,12,15,20,21,23,24,25,29,32) AND `field` = 'sta');

INSERT INTO `ai_playerbot_weightscale_data` (`id`, `field`, `val`) VALUES
	(24, 'arcsplpwr', 2),
	(25, 'arcsplpwr', 2),
	(6, 'sta', 1),
	(7, 'sta', 1),
	(8, 'sta', 1),
	(9, 'sta', 1),
	(10, 'sta', 1),
	(11, 'sta', 1),
	(12, 'sta', 1),
	(15, 'sta', 1),
	(20, 'sta', 1),
	(21, 'sta', 1),
	(23, 'sta', 1),
	(24, 'sta', 1),
	(25, 'sta', 1),
	(29, 'sta', 1),
	(32, 'sta', 1);
