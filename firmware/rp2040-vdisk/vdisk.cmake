# Included from firmware/CMakeLists.txt after the remapper target is defined.

if(NOT DEFINED VDISK_ENABLE)
    set(VDISK_ENABLE ON)
endif()

if(VDISK_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/vdisk.cc)
    target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(remapper PRIVATE
        VDISK_ENABLED
        CFG_TUD_MSC=1
        CFG_TUD_MSC_EP_BUFSIZE=512
    )
endif()
