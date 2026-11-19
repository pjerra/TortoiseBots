# Gear replacement rule (hire gear tiers 1-7 + autogear tier 0)

Target: every (class, spec, slot, tier) row must point at an item that EXISTS on Forever
(Item.db2 binary hit or world DB armory_item row). Client build: repack DB2s = 70888 (target 70938).

Rule, per row of tools/hire_gear/data/hire_gear.csv (slot = TB slot 0-17, tier 1-7):
1. Item exists on Forever -> keep, forever_id = same id.
2. Else substitute the nearest existing item with the same class + spec + slot,
   nearest tier at or below the row's tier (cheaper is safe), else the lowest above.
3. If no item exists in that class+spec+slot at any tier -> leave empty for hypeer.
   (Did not happen: all 397 missing rows found a same-slot substitute.)

Result: 2994 keep, 397 replaced, 0 empty. item_level/required_level from armory_item
where available (custom rows); vanilla rows carry the hire-gear name, stats resolve
from the client DB2 at runtime. Tier 0 (autogear templates in item_tiers.csv) are
unslotted pools, not per-slot rows - the per-slot table covers tiers 1-7; tier-0 pools
were checked for existence in items-missing-classified.csv instead.

Enchants (hire_gear.csv `enchant` column, spell ids) are NOT remapped here - they go
through the spell table (forever-spell-ids.csv); an enchant whose spell is MISSING
there must be dropped from the row on import.
