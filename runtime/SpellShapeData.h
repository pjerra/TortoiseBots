#pragma once

namespace TortoiseBots {

// Area shapes of boss and elite spells whose hit area points somewhere (the dashboard's boss overlay, fight debugger
// phase 2). DATA, not code: runtime/SpellShape.h parses this text and the module sends no shape for a spell absent from
// it, so a raid adds rows here, never a branch in the game code. Columns:
//
//   spell,shape,angle deg,reach yd,width yd,source
//
// shape: cone (in front), back-cone (behind), line (along the facing). angle is the full arc the core fills, 0 for a
// line; reach is the distance from the caster along its facing; width is the band across a line, 0 for a cone. Units
// are the game's yards. A trailing source says where each number was read.
//
// Core (Tortoise 1.18.1, ~/tortoise-wow): tw_world.spell_template row `entry`; effectImplicitTargetA 24 is an enemy
// cone whose arc the core picks by spellVisual (src/game/Spells/Spell.cpp:2751-2768) and fills with HasInArc
// (Spell.cpp:8258-8272): 2*pi/3 = 120 degrees in front, 75 degrees behind for spellVisual 3879. Target 54 is a narrower
// cone: Spell.cpp:2771-2840 fills a 90 degree arc unless the spell id is one of its own rotated 120 degree cases. The
// reach is effectRadiusIndex -> SpellRadius.dbc yards (9=20, 10=30, 11=45, 13=10, 21=35, 23=40). A spell whose target is a
// single unit (effectImplicitTargetA 6, e.g. Cleave) or a circle around the caster has no row: there is no arc to draw.
//
// The Deep Breath lanes are chains of link spells fired along a straight lane, 30 yd radius around each link (radius
// index 10), so the lane is 60 yd wide. A lane row's reach is the lane the boss is stopped on: its hover point (the
// ai_boss_stand anchor) to the opposite hover point (that row's release anchor), ~67-92 yd, which is what the
// dashboard draws from her position; 17086's chain runs on into the tunnel past the south point, and that tail is not
// drawn. Chain lengths from tw_world.spell_target_position are noted per row.
inline char const* const kSpellShapeCsv = R"(
# spell,shape,angle,reach,width,source
18435,cone,120,45,0,spell_template 18435 Flame Breath: target 24; radius 11 = 45 yd; front 120
23461,cone,120,45,0,spell_template 23461 Flame Breath: target 24 x2; radius 11 = 45 yd; front 120
18500,cone,120,20,0,spell_template 18500 Wing Buffet: target 24 x2; radius 9 = 20 yd; front 120
15847,back-cone,75,30,0,spell_template 15847 Tail Sweep: target 24 x2; spellVisual 3879 = back 75; radius 10 = 30 yd
19272,cone,90,35,0,spell_template 19272 Lava Breath: target 54 (90 degree arc); radius 21 = 35 yd; front
24839,cone,120,20,0,spell_template 24839 Acid Breath: target 24 x2; radius 9 = 20 yd; front 120
25814,cone,120,10,0,spell_template 25814 Wide Slash: target 24; radius 13 = 10 yd; front 120
26102,cone,90,40,0,spell_template 26102 Sand Blast: target 54 x2 (90 degree arc); radius 23 = 40 yd; front
17086,line,0,88.8,60,ai_boss_stand 249/10184 anchors 8->4 (N to S hover points, 88.8 yd); 20-link spell_target_position chain 151.5 yd runs on into the tunnel; radius 10 = 30 yd each side
18351,line,0,88.8,60,ai_boss_stand 249/10184 anchors 4->8 (S to N hover points, 88.8 yd); 11-link spell_target_position chain 89.6 yd; radius 10 = 30 yd each side
18576,line,0,67.5,60,ai_boss_stand 249/10184 anchors 2->6 (E to W hover points, 67.5 yd); 7-link spell_target_position chain 55.1 yd; radius 10 = 30 yd each side
18609,line,0,67.5,60,ai_boss_stand 249/10184 anchors 6->2 (W to E hover points, 67.5 yd); 7-link spell_target_position chain 55.1 yd; radius 10 = 30 yd each side
18564,line,0,92.4,60,ai_boss_stand 249/10184 anchors 3->7 (SE to NW hover points, 92.4 yd); 12-link spell_target_position chain 86.6 yd; radius 10 = 30 yd each side
18584,line,0,92.4,60,ai_boss_stand 249/10184 anchors 7->3 (NW to SE hover points, 92.4 yd); 12-link spell_target_position chain 86.6 yd; radius 10 = 30 yd each side
18596,line,0,90.3,60,ai_boss_stand 249/10184 anchors 5->1 (SW to NE hover points, 90.3 yd); 12-link spell_target_position chain 89.0 yd; radius 10 = 30 yd each side
18617,line,0,90.3,60,ai_boss_stand 249/10184 anchors 1->5 (NE to SW hover points, 90.3 yd); 12-link spell_target_position chain 89.0 yd; radius 10 = 30 yd each side
)";

} // namespace TortoiseBots
