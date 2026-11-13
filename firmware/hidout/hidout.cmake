# Included from firmware/CMakeLists.txt after the remapper target is defined.

if(NOT DEFINED HIDOUT_ENABLE)
    set(HIDOUT_ENABLE ON)
endif()
if(NOT DEFINED HIDOUT_REGISTER)
    set(HIDOUT_REGISTER 1)
endif()
if(NOT DEFINED HIDOUT_TARGETS)
    set(HIDOUT_TARGETS 3)
endif()

if(HIDOUT_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/hidout.cc)
    target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(remapper PRIVATE
        HIDOUT_ENABLED
        HIDOUT_REGISTER=${HIDOUT_REGISTER}
        HIDOUT_TARGETS=${HIDOUT_TARGETS}
    )
endif()
