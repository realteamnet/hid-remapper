# Included from firmware/CMakeLists.txt after the remapper target is defined.

if(NOT DEFINED VDISK_ENABLE)
    set(VDISK_ENABLE ON)
endif()
if(NOT DEFINED VDISK_WEB_TOOL)
    set(VDISK_WEB_TOOL ON)
endif()

if(VDISK_ENABLE)
    # vdisk.cc reads and writes the config format; stop the build if upstream changes it
    file(READ ${CMAKE_CURRENT_LIST_DIR}/vdisk.h VDISK_H)
    string(REGEX MATCH "#define VDISK_CONFIG_VERSION ([0-9]+)" _ "${VDISK_H}")
    set(VDISK_VERSION ${CMAKE_MATCH_1})
    file(READ ${CMAKE_CURRENT_LIST_DIR}/../src/config.cc VDISK_CONFIG_CC)
    string(REGEX MATCH "CONFIG_VERSION = ([0-9]+);" _ "${VDISK_CONFIG_CC}")
    if(NOT CMAKE_MATCH_1 STREQUAL VDISK_VERSION)
        message(FATAL_ERROR "vdisk supports config version ${VDISK_VERSION}, but src/config.cc is version ${CMAKE_MATCH_1}. Update vdisk.cc, then VDISK_CONFIG_VERSION in vdisk.h, or build with -DVDISK_ENABLE=OFF.")
    endif()
    file(READ ${CMAKE_CURRENT_LIST_DIR}/../src/types.h VDISK_TYPES_H)
    string(REGEX MATCH "enum class Op [^}]*" VDISK_OPS "${VDISK_TYPES_H}")
    string(REGEX MATCHALL "= [0-9]+," VDISK_OPS "${VDISK_OPS}")
    list(GET VDISK_OPS -1 VDISK_LAST_OP)
    string(REGEX MATCH "[0-9]+" VDISK_LAST_OP "${VDISK_LAST_OP}")
    math(EXPR VDISK_OP_COUNT "${VDISK_LAST_OP} + 1")

    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/vdisk.cc)
    target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(remapper PRIVATE
        VDISK_ENABLED
        VDISK_OP_COUNT=${VDISK_OP_COUNT}
        CFG_TUD_MSC=1
        CFG_TUD_MSC_EP_BUFSIZE=512
    )

    if(VDISK_WEB_TOOL)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(VDISK_TOOL_SRC ${CMAKE_CURRENT_LIST_DIR}/../../config-tool-web)
        set(VDISK_TOOL_HTML ${CMAKE_CURRENT_BINARY_DIR}/vdisk_webtool.htm)
        add_custom_command(
            OUTPUT ${VDISK_TOOL_HTML}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/webtool.py ${VDISK_TOOL_SRC} ${VDISK_TOOL_HTML}
            DEPENDS
                ${CMAKE_CURRENT_LIST_DIR}/webtool.py
                ${VDISK_TOOL_SRC}/index.html
                ${VDISK_TOOL_SRC}/code.js
                ${VDISK_TOOL_SRC}/crc.js
                ${VDISK_TOOL_SRC}/usages.js
                ${VDISK_TOOL_SRC}/examples.js
            VERBATIM
        )
        # .flashdata keeps the file in flash; the rest of the firmware is copied to RAM
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/vdisk_webtool.S
"    .section .flashdata.vdisk_tool, \"a\"
    .global vdisk_tool_start
    .global vdisk_tool_end
vdisk_tool_start:
    .incbin \"${VDISK_TOOL_HTML}\"
vdisk_tool_end:
")
        set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/vdisk_webtool.S PROPERTIES OBJECT_DEPENDS ${VDISK_TOOL_HTML})
        target_sources(remapper PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/vdisk_webtool.S)
        target_compile_definitions(remapper PRIVATE VDISK_WEB_TOOL)
    endif()
endif()
