# rp2040-ws2812 build hook. Included from firmware/CMakeLists.txt with
#   include(rp2040-ws2812/ws2812.cmake OPTIONAL)
# after the `remapper` target is defined. All module build logic lives here so
# the upstream CMakeLists.txt edit is a single line.

set(WS2812_DIR ${CMAKE_CURRENT_LIST_DIR})

# Boards whose `remapper` target gets the LED. The Waveshare RP2040-Zero runs
# the `pico` build (WS2812 on GPIO16, which that build uses as UART TX, so the
# pin is already excluded from remappable GPIOs).
if(NOT DEFINED WS2812_BOARDS)
    set(WS2812_BOARDS pico)
endif()
if(NOT DEFINED WS2812_PIN)
    set(WS2812_PIN 16)
endif()

# Standalone proof-of-concept firmware: ws2812_test.uf2 (RP2040 only).
if(PICO_PLATFORM STREQUAL "rp2040")
    add_executable(ws2812_test
        ${WS2812_DIR}/ws2812_test.cc
        ${WS2812_DIR}/ws2812_led.cc
    )
    pico_generate_pio_header(ws2812_test ${WS2812_DIR}/ws2812.pio)
    target_include_directories(ws2812_test PRIVATE ${WS2812_DIR})
    target_compile_definitions(ws2812_test PRIVATE WS2812_PIN=${WS2812_PIN})
    target_link_libraries(ws2812_test pico_stdlib hardware_pio)
    pico_add_extra_outputs(ws2812_test)
endif()

if(PICO_BOARD IN_LIST WS2812_BOARDS)
    target_sources(remapper PRIVATE ${WS2812_DIR}/ws2812_led.cc)
    pico_generate_pio_header(remapper ${WS2812_DIR}/ws2812.pio)
    target_include_directories(remapper PRIVATE ${WS2812_DIR})
    target_compile_definitions(remapper PRIVATE WS2812_ENABLED WS2812_PIN=${WS2812_PIN})
endif()
