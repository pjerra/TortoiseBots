// The <Mercenary Hire> recruiter NPCs on ForeverCore (issue #298, package P3): the CreatureAI registered for
// them and the gossip wizard it builds. Built only with TORTOISEBOTS_SLICE1; the option-OFF build gets an
// empty stub (CMake globs forever/src/host/hire/*.cpp in both builds).
#pragma once

namespace TortoiseBots::hire
{
// Registers the recruiter AI from AddSC_tortoisebots. The SQL sets creature_template.ScriptName to the same
// name; the other gossip pages return to this one with recruiter->AI()->OnGossipHello(player).
void RegisterRecruiterScripts();
}
