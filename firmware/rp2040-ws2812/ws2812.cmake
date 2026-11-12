# rp2040-ws2812 build hook. Included from firmware/CMakeLists.txt with
#   include(rp2040-ws2812/ws2812.cmake OPTIONAL)
# after the `remapper` target is defined. All module build logic lives here so
# the upstream CMakeLists.txt edit is a single line.
#
# The module is added to the `remapper` target on every board. It is dormant
# (no PIO state machine, no pin) until the config writes the settings register,
# so there is nothing board specific to set here.

set(WS2812_DIR ${CMAKE_CURRENT_LIST_DIR})

# Set to OFF to leave the module out of the build entirely.
if(NOT DEFINED WS2812_ENABLE)
    set(WS2812_ENABLE ON)
endif()

# Expression registers (1-32) the LED reads: settings 0x00LOOOPP and colour
# 0xIIRRGGBB. Change these if a config/project needs registers 31/32 for
# something else, e.g. cmake -DWS2812_COLOR_REGISTER=8 -DWS2812_SETTINGS_REGISTER=7.
if(NOT DEFINED WS2812_SETTINGS_REGISTER)
    set(WS2812_SETTINGS_REGISTER 31)
endif()
if(NOT DEFINED WS2812_COLOR_REGISTER)
    set(WS2812_COLOR_REGISTER 32)
endif()

# Settings used by the standalone diagnostic firmware only (same format as the
# settings register). Default: GPIO16, RGB order = Waveshare RP2040-Zero clone.
if(NOT DEFINED WS2812_TEST_SETTINGS)
    set(WS2812_TEST_SETTINGS 0x00012310)
endif()

if(WS2812_ENABLE)
    target_sources(remapper PRIVATE ${WS2812_DIR}/ws2812_led.cc)
    pico_generate_pio_header(remapper ${WS2812_DIR}/ws2812.pio)
    target_include_directories(remapper PRIVATE ${WS2812_DIR})
    target_compile_definitions(remapper PRIVATE
        WS2812_ENABLED
        WS2812_SETTINGS_REGISTER=${WS2812_SETTINGS_REGISTER}
        WS2812_COLOR_REGISTER=${WS2812_COLOR_REGISTER})

    # Standalone diagnostic firmware: ws2812_test.uf2 (RP2040 only, not built
    # by default: `make ws2812_test`).
    if(PICO_PLATFORM STREQUAL "rp2040")
        add_executable(ws2812_test EXCLUDE_FROM_ALL
            ${WS2812_DIR}/ws2812_test.cc
            ${WS2812_DIR}/ws2812_led.cc
        )
        pico_generate_pio_header(ws2812_test ${WS2812_DIR}/ws2812.pio)
        target_include_directories(ws2812_test PRIVATE ${WS2812_DIR})
        target_compile_definitions(ws2812_test PRIVATE WS2812_TEST_SETTINGS=${WS2812_TEST_SETTINGS})
        target_link_libraries(ws2812_test pico_stdlib hardware_pio)
        # Run from RAM like `remapper`: XIP from flash with the Pico's
        # boot2/flash settings did not run on the RP2040-Zero clone.
        pico_set_binary_type(ws2812_test copy_to_ram)
        pico_add_extra_outputs(ws2812_test)
    endif()
endif()
