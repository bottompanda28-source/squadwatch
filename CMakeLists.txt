cmake_minimum_required(VERSION 3.20)
project(SquadWatch LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)

# nlohmann/json (MIT)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
endif()

add_library(squadwatch_core STATIC
  src/core/unicode_tables.cpp src/core/text.cpp src/core/match.cpp
  src/core/watchlist.cpp src/core/roster.cpp src/core/roles.cpp src/core/history.cpp src/core/exchange.cpp src/core/leaves.cpp)
target_include_directories(squadwatch_core PUBLIC src/core ${json_SOURCE_DIR}/single_include)
if(MSVC)
  target_compile_options(squadwatch_core PUBLIC /utf-8 /W3)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core squadwatch_core)
add_test(NAME core COMMAND test_core)

if(WIN32)
  # Nexus API header (MIT, Raidcore) pinned to a commit; Dear ImGui v1.80 must match the version Nexus ships.
  FetchContent_Declare(nexusapi GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git GIT_TAG 9b2c53df86c00db6495642bfcff2d0611bd957ef)
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.80 GIT_SHALLOW TRUE)
  foreach(dep nexusapi imgui)
    FetchContent_GetProperties(${dep})
    if(NOT ${dep}_POPULATED)
      FetchContent_Populate(${dep})
    endif()
  endforeach()

  add_library(squad_watch SHARED
    src/addon/addon.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
  target_include_directories(squad_watch PRIVATE ${nexusapi_SOURCE_DIR} ${imgui_SOURCE_DIR} src/addon)
  target_compile_definitions(squad_watch PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(squad_watch PRIVATE squadwatch_core)
  set_target_properties(squad_watch PROPERTIES PREFIX "")
endif()
