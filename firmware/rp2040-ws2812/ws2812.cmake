# Included from firmware/CMakeLists.txt after the remapper target is defined.

if(NOT DEFINED WS2812_ENABLE)
    set(WS2812_ENABLE ON)
endif()
if(NOT DEFINED WS2812_SETTINGS_REGISTER)
    set(WS2812_SETTINGS_REGISTER 31)
endif()
if(NOT DEFINED WS2812_COLOR_REGISTER)
    set(WS2812_COLOR_REGISTER 32)
endif()

if(WS2812_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ws2812_led.cc)
    pico_generate_pio_header(remapper ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
    target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(remapper PRIVATE
        WS2812_ENABLED
        WS2812_SETTINGS_REGISTER=${WS2812_SETTINGS_REGISTER}
        WS2812_COLOR_REGISTER=${WS2812_COLOR_REGISTER}
    )
endif()
