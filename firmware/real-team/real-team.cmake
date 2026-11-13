# Included from firmware/CMakeLists.txt after the remapper target is defined.
# Pulls in the modules below; each one has its own <NAME>_ENABLE setting (default ON).

include(${CMAKE_CURRENT_LIST_DIR}/ws2812/ws2812.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/vdisk/vdisk.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/hidout/hidout.cmake)

target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/real_team.cc)
target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(remapper PRIVATE REAL_TEAM_ENABLED)
