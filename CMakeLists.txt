# TortoiseBots for ForeverCore (Karrz1/TrinityCore, branch `forever`).
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
# (host/TbH3Off.cpp) that gives their "the engine is not built in" answers, and the slice build compiles the real four instead.
if(NOT TORTOISEBOTS_SLICE1)
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/compat/(TbCompat_C|TbCompat_Config|TbCompat_Unported)\\.cpp$")
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/(TbH3|TbOrders|TbPartyState|TbTestKit)\\.cpp$")
else()
  list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/forever/src/host/TbH3Off\\.cpp$")
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

set_target_properties(tortoisebots
  PROPERTIES
    FOLDER "src/modules/TortoiseBots")

if(TORTOISEBOTS_SLICE1)
  # Slice 2 (R3): the option keeps its name TORTOISEBOTS_SLICE1 (the macro of the host code and the build scripts) and reads the slice-4 list (forever/SLICE4.txt),
  # which holds every file of SLICE1.txt, SLICE2.txt and SLICE3.txt plus the dead-strategy, release and resurrection files (forever/docs/SLICE4-P-NOTES.md).
  # Slice 9 (L9): forever/SLICE9.txt = the slice-4 list plus LootRollAction.cpp and ItemUsageValue.cpp (the merge worker combines it with SLICE8).
  set(TB_SLICE_LIST ${CMAKE_CURRENT_SOURCE_DIR}/forever/SLICE9.txt)
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
      message(FATAL_ERROR "TortoiseBots: forever/SLICE9.txt names ${line}, which does not exist")
    endif()
    list(APPEND TB_SLICE_LEGACY ${CMAKE_CURRENT_SOURCE_DIR}/${line})
  endforeach()

  file(GLOB TB_SLICE_NEW CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/slice/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/src/host/TbHost*.cpp)

  target_sources(tortoisebots PRIVATE ${TB_SLICE_LEGACY} ${TB_SLICE_NEW})

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
