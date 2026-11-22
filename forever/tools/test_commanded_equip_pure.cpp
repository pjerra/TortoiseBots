// Truth table for the commanded-bot auto-equip gate (TB parity):
// commanded bots self-equip iff AiPlayerbot.ClaimedBotSelfEquip (default 1);
// the explicit `.tb gear upgrade` order always runs; free bots always run.
#include "forever/src/host/TbCommandedEquipPure.h"

#include <cassert>

int main()
{
    using TortoiseBots::commandedequip::MayCommandedBotAutoEquip;

    // Free bots always run, whatever the switch says.
    assert(MayCommandedBotAutoEquip(false, true, false));
    assert(MayCommandedBotAutoEquip(false, false, false));

    // Commanded bots follow the switch: on wears, off does not.
    assert(MayCommandedBotAutoEquip(true, true, false));
    assert(!MayCommandedBotAutoEquip(true, false, false));

    // The explicit order runs regardless of group and switch.
    assert(MayCommandedBotAutoEquip(false, true, true));
    assert(MayCommandedBotAutoEquip(false, false, true));
    assert(MayCommandedBotAutoEquip(true, true, true));
    assert(MayCommandedBotAutoEquip(true, false, true));

    return 0;
}
