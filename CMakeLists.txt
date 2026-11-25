# TortoiseBots for ForeverCore (Karrz1/TrinityCore, branch `forever-bots`).
#
# This file is only for ForeverCore. The Turtle (tortoise-wow) build does not read it: that core includes
# TortoiseBots.cmake and collects src/. Clone or link this repository at  <core>/src/modules/TortoiseBots  and
# configure the core with  -DBUILD_TORTOISEBOTS=1 .
# The core is not changed: the module only uses the public game API and the ScriptMgr hooks.
# Pinned core commit: forever/PINNED_CORE.
#
# forever/src  holds the code written for ForeverCore (session, host layer, scripts).
# The repository root is on the include path, so the core-free decision code shared with the Turtle build
# (ai/playerbot/*Policy.h, runtime/policy/*.h) is included by its repository path.

if(NOT COMMAND trinity_add_module)
  return()
endif()

option(BUILD_TORTOISEBOTS "Build the TortoiseBots module" 0)
if(NOT BUILD_TORTOISEBOTS)
  return()
endif()

# Two bot systems must never load together (two `.bot` command sets, two session drivers on the PlayerScript hooks, two group and
# auction loops). The forever-bots core carries Karrz's own PlayerbotV2 module, which is off unless BUILD_PLAYERBOT_V2=1; this module
# stops the configure when both are asked for. forever/tools/test_playerbot_v2_guard.py runs the configure both ways.
if(BUILD_PLAYERBOT_V2)
  message(FATAL_ERROR "TortoiseBots (BUILD_TORTOISEBOTS) cannot be built together with the PlayerbotV2 module (BUILD_PLAYERBOT_V2): "
    "two bot systems must never load together. Configure with -DBUILD_PLAYERBOT_V2=OFF, or with -DBUILD_TORTOISEBOTS=OFF.")
endif()

# (The core README shows CollectSourceFiles(); that macro does not exist at the pinned commit, so glob here.)
file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/*.h)

# forever/src/slice holds the sources of the slice-1 build (the AiObjectContext constructor, the AiFactory, the
# missing-key guard); they belong to the TORTOISEBOTS_SLICE1 build only and are added below when it is ON.
# forever/src/host/TbHost*.cpp are the host services that answer the legacy engine's questions (R6B): they include legacy
# headers, so they too belong to the TORTOISEBOTS_SLICE1 build only.
list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/(slice/[^/]*|host/TbHost[^/]*)\\.cpp$")

# Slice 1 (forever/docs/SLICE1-R2-NOTES.md): the legacy bot brain files listed in forever/SLICE1.txt, built next to the
# module. OFF (the default) leaves the module exactly as it was: nothing below this option runs.
option(TORTOISEBOTS_SLICE1 "Also build the slice-1 legacy bot engine (forever/SLICE1.txt)" OFF)

# The option-OFF build is the spike plus the item and spell mirrors and nothing more (R9, findings R12): the compat sources that only the engine
# needs (the R4C helpers, the legacy ini reader, the unported-feature counters) are not compiled into it. TbScripts.cpp and TbEngineBrain.cpp
# answer `.tb engine` / `.tb unported` with "the engine is not built in" and never name them without the option.
# Slice 3 (M): the nine group-fight commands (host/TbH3, TbOrders, TbPartyState, TbTestKit) are engine-facing code; the default build compiles ONE stub
# (host/TbH3Off.cpp) that gives their "the engine is not built in" answers, and the slice build compiles the real four instead. S1-R2's `.tb hear`
# follows the same rule (host/chat/TbChatCmd.cpp: it names the engine's reply types); the chat hook itself (TbChatIn.cpp) is in both builds and inert here.
if(NOT TORTOISEBOTS_SLICE1)
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/compat/(TbCompat_C|TbCompat_Config|TbCompat_Unported|TbCompat_V|TbCompat_WL4)\\.cpp$")
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/(TbH3|TbOrders|TbPartyState|TbRaidLayout|TbTestKit|TbLuaBots|chat/TbChatCmd)\\.cpp$")
  # The hire provisioner is the engine's (it logs a bot in, levels it and gears it): forever/src/host/hire is slice-only.
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/hire/[^/]*\\.cpp$")
else()
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/TbH3Off\\.cpp$")
  # TBF-B1 third review finding 9: the provider is compiled only when the engine is built too (without BUILD_FOREVER_LUA the whole
  # file is one empty translation unit, so nothing of it may enter the library).
  if(NOT BUILD_FOREVER_LUA)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/TbLuaBots\\.cpp$")
  endif()
endif()

add_library(tortoisebots STATIC ${PRIVATE_SOURCES})

target_include_directories(tortoisebots
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/src
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(tortoisebots
  PUBLIC
    TORTOISEBOTS_CORE_FOREVER=1)

target_link_libraries(tortoisebots
  PRIVATE
    trinity-core-interface
  PUBLIC
    game-interface)

# ForeverLua bot API (SPEC-forever-lua-bot-api.md section 1, TBF-B1): with the engine module built in the same tree, the
# module's provider (forever/src/host/TbLuaBots.cpp) is compiled and linked against the game-free engine. host/TbLuaBots.cpp
# belongs to the slice build only (the filter above); the whole file is wrapped in
# `#if TORTOISEBOTS_SLICE1 && TBF_WITH_FOREVER_LUA`, so without the engine it compiles to nothing.
if(BUILD_FOREVER_LUA)
  # TBF-B1 needs the engine's bot API (forever-lua 01c9a36, src/engine/BotProvider.h). Without the header the build used to
  # stop at compile time with a plain "No such file"; it stops here with what to do instead. The engine module sits beside
  # this module in the core (src/modules/ForeverLua), so the path is relative and works whichever module configures first.
  set(TB_ENGINE_BOT_PROVIDER "${CMAKE_CURRENT_SOURCE_DIR}/../ForeverLua/src/engine/BotProvider.h")
  if(NOT EXISTS "${TB_ENGINE_BOT_PROVIDER}")
    message(FATAL_ERROR "TortoiseBots: BUILD_FOREVER_LUA=1 but the ForeverLua engine is not beside the module: "
      "expected ${TB_ENGINE_BOT_PROVIDER} (forever-lua >= 01c9a36). Clone or link forever-lua at <core>/src/modules/ForeverLua, "
      "or configure with -DBUILD_FOREVER_LUA=0.")
  endif()
  target_compile_definitions(tortoisebots PRIVATE TBF_WITH_FOREVER_LUA=1)
  target_link_libraries(tortoisebots PRIVATE forever-lua-engine)
endif()

set_target_properties(tortoisebots
  PROPERTIES
    FOLDER "src/modules/TortoiseBots")

if(TORTOISEBOTS_SLICE1)
  # Slice 2 (R3): the option keeps its name TORTOISEBOTS_SLICE1 (the macro of the host code and the build scripts) and reads the newest list (forever/SLICE-W4.txt: the W1 list plus the W4 travel files),
  # which holds every file of SLICE1.txt .. SLICE5.txt (the plan loader DungeonPlanMgr.cpp, BossTacticsMgr.cpp; forever/docs/RUN-S5-P-NOTES.md), the L9 files of
  # slice 9 (LootRollAction.cpp, ItemUsageValue.cpp; forever/docs/SLICE9-L-NOTES.md; the doors package adds no legacy file), and every file of the slice-8, slice-10
  # and slice-11 lists that integration batch 2 brought in (mage, rogue, give item, the 40 druid/paladin/shaman files, hunter, warlock, pets:
  # forever/docs/SLICE8-K-NOTES.md, SLICE10-K-NOTES.md, SLICE11-NOTES.md). It is the newest list; SLICE5, SLICE8, SLICE10 and SLICE11 are subsets of it.
  set(TB_SLICE_LIST ${CMAKE_CURRENT_SOURCE_DIR}/forever/SLICE-W4.txt)
  set(TB_SLICE_PREINCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/compat/botpch_forever.h)
  # Re-run CMake when the list changes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TB_SLICE_LIST})

  # Files of the list that the slice replaces: ai/playerbot/AiFactory.cpp by forever/src/slice/SliceAiFactory.cpp, and the
  # full AiObjectContext constructor (ai/playerbot/strategy/AiObjectContextRegistry.cpp, which package R1 splits out of
  # AiObjectContext.cpp) by forever/src/slice/SliceAiObjectContext.cpp. Excluded whether or not they exist yet or are listed.
  set(TB_SLICE_REPLACED
    ai/playerbot/AiFactory.cpp
    ai/playerbot/strategy/AiObjectContextRegistry.cpp)

  file(STRINGS ${TB_SLICE_LIST} TB_SLICE_LINES)
  set(TB_SLICE_LEGACY "")
  foreach(line IN LISTS TB_SLICE_LINES)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
      continue()
    endif()
    if(line IN_LIST TB_SLICE_REPLACED)
      continue()
    endif()
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${line})
      message(FATAL_ERROR "TortoiseBots: forever/SLICE-W4.txt names ${line}, which does not exist")
    endif()
    list(APPEND TB_SLICE_LEGACY ${CMAKE_CURRENT_SOURCE_DIR}/${line})
  endforeach()

  file(GLOB TB_SLICE_NEW CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/slice/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/host/TbHost*.cpp)

  target_sources(tortoisebots PRIVATE ${TB_SLICE_LEGACY} ${TB_SLICE_NEW})

  # The hire fee is the one TortoiseBots formula (runtime/HireCost.h): core-free, so it needs no preinclude.
  target_sources(tortoisebots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/runtime/HireCost.cpp)

  # The run trace and the why trace: the shared Turtle runtime sources (docs/server/run-trace.md, docs/server/
  # why-trace.md); the host glue is forever/src/host/cmd/TbCmdTrace.cpp. They read the engine and the core, so they get
  # the legacy files' preinclude.
  set(TB_SLICE_TRACE_RUNTIME
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/RunTrace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/RunTraceWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/WhyTrace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/DecisionCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/DecisionCaptureFiles.cpp)
  target_sources(tortoisebots PRIVATE ${TB_SLICE_TRACE_RUNTIME})
  if(MSVC)
    set_source_files_properties(${TB_SLICE_TRACE_RUNTIME} PROPERTIES COMPILE_OPTIONS "/FI${TB_SLICE_PREINCLUDE}")
  else()
    set_source_files_properties(${TB_SLICE_TRACE_RUNTIME} PROPERTIES COMPILE_OPTIONS "-include;${TB_SLICE_PREINCLUDE}")
  endif()

  # The fight monitor's shared runtime TU (the Turtle file; its one world read and its two log lines go through ai/playerbot/TbCompatC5.h).
  # Only this build registers its scripts and feeds it (forever/src/host/cmd/TbCmdCombat.cpp); the option-OFF build has neither. It reads the
  # engine's config and logs through the legacy proxy, so it gets the legacy files' preinclude.
  target_sources(tortoisebots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/runtime/CombatMonitorService.cpp)
  if(MSVC)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/runtime/CombatMonitorService.cpp PROPERTIES COMPILE_OPTIONS "/FI${TB_SLICE_PREINCLUDE}")
  else()
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/runtime/CombatMonitorService.cpp PROPERTIES COMPILE_OPTIONS "-include;${TB_SLICE_PREINCLUDE}")
  endif()

  # The ForeverCore counterpart of ai/botpch.h goes in front of the legacy files only: the files under forever/src are
  # ordinary module code (the two slice sources that need it include it themselves).
  if(MSVC)
    set_source_files_properties(${TB_SLICE_LEGACY} PROPERTIES COMPILE_OPTIONS "/FI${TB_SLICE_PREINCLUDE}")
  else()
    set_source_files_properties(${TB_SLICE_LEGACY} PROPERTIES COMPILE_OPTIONS "-include;${TB_SLICE_PREINCLUDE}")
  endif()

  # What forever/tools/port_census.py compiles the legacy files with: forever/compat and forever/src/compat first, the
  # eight old include folders of the Turtle build (TortoiseBots.cmake) AFTER the core's own (-idirafter), so a legacy
  # header never shadows a core header of the same name.
  target_include_directories(tortoisebots PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/compat
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/compat)
  set(TB_SLICE_OLD_DIRS
    ai ai/playerbot ai/playerbot/strategy ai/playerbot/strategy/actions ai/playerbot/strategy/triggers
    ai/playerbot/strategy/values host runtime)
  foreach(dir IN LISTS TB_SLICE_OLD_DIRS)
    if(MSVC)
      target_include_directories(tortoisebots PRIVATE AFTER ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    else()
      target_compile_options(tortoisebots PRIVATE "SHELL:-idirafter ${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    endif()
  endforeach()

  # Debug info for the module's own objects only (a backtrace with arguments under gdb); the core stays as built.
  option(TORTOISEBOTS_DEBUG_INFO "Compile the module with -g (gdb backtraces with arguments)" OFF)
  if(TORTOISEBOTS_DEBUG_INFO AND NOT MSVC)
    target_compile_options(tortoisebots PRIVATE -g -fno-omit-frame-pointer)
  endif()

  # The legacy packet tables put ForeverCore's 32-bit opcodes into 16-bit fields (they are never registered there). The core
  # gives clang -Wno-narrowing but not GCC, so a GCC build stops on them: give GCC the same.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(tortoisebots PRIVATE -Wno-narrowing)
  endif()

  target_compile_definitions(tortoisebots
    PUBLIC
      TORTOISEBOTS_SLICE1=1
    PRIVATE
      MANGOSBOT_ZERO=1
      CMANGOS=1)
endif()

set(TB_MODULE_CONFIGS ${CMAKE_CURRENT_SOURCE_DIR}/forever/conf/tortoisebots.conf.dist)
if(TORTOISEBOTS_SLICE1)
  # The engine's own settings (forever/conf/aiplayerbot.conf.dist, generated by forever/tools/gen_aiplayerbot_conf.py). The
  # core copies CONFIGS to worldserver.conf.d/, which only the core's parser reads: the engine reads the file itself, through
  # its own Config class, as `<directory of worldserver.conf>/aiplayerbot.conf`. The deployment (forever-run/setup.sh) copies it
  # there; TbEngineBrain::EnsureConfig says so when it is missing (findings item 19).
  list(APPEND TB_MODULE_CONFIGS ${CMAKE_CURRENT_SOURCE_DIR}/forever/conf/aiplayerbot.conf.dist)
endif()

trinity_add_module(tortoisebots AddSC_tortoisebots
  CONFIGS
    ${TB_MODULE_CONFIGS})
