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

trinity_add_module(tortoisebots AddSC_tortoisebots
  CONFIGS
    ${CMAKE_CURRENT_SOURCE_DIR}/forever/conf/tortoisebots.conf.dist)
