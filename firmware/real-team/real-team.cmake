# Included from firmware/CMakeLists.txt after the remapper target is defined.
# Settings (-D...): see README.md, "Build settings".

foreach(setting WS2812_ENABLE HIDOUT_ENABLE)
    if(NOT DEFINED ${setting})
        set(${setting} ON)
    endif()
endforeach()
if(NOT DEFINED WS2812_SETTINGS_REGISTER)
    set(WS2812_SETTINGS_REGISTER 31)
endif()
if(NOT DEFINED WS2812_COLOR_REGISTER)
    set(WS2812_COLOR_REGISTER 32)
endif()
if(NOT DEFINED HIDOUT_REGISTER)
    set(HIDOUT_REGISTER 1)
endif()

target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/real_team.cc)
target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(remapper PRIVATE REAL_TEAM_ENABLED)

if(WS2812_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ws2812.cc)
    pico_generate_pio_header(remapper ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
    target_compile_definitions(remapper PRIVATE
        WS2812_ENABLED
        WS2812_SETTINGS_REGISTER=${WS2812_SETTINGS_REGISTER}
        WS2812_COLOR_REGISTER=${WS2812_COLOR_REGISTER}
    )
    foreach(setting WS2812_BOOT_SETTINGS WS2812_BOOT_COLOR)
        if(DEFINED ${setting})
            target_compile_definitions(remapper PRIVATE ${setting}=${${setting}})
        endif()
    endforeach()
endif()

if(HIDOUT_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/hidout.cc)
    target_compile_definitions(remapper PRIVATE
        HIDOUT_ENABLED
        HIDOUT_REGISTER=${HIDOUT_REGISTER}
    )
endif()

# the command table (numbers-only expression lines) is used by hidout
if(HIDOUT_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cmd_table.c ${CMAKE_CURRENT_LIST_DIR}/cmd_table_remapper.cc)
    target_compile_definitions(remapper PRIVATE REAL_TEAM_CMD_TABLE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/vdisk/vdisk.cmake)
