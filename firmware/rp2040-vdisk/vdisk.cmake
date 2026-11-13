# Included from firmware/CMakeLists.txt after the remapper target is defined.

if(NOT DEFINED VDISK_ENABLE)
    set(VDISK_ENABLE ON)
endif()
if(NOT DEFINED VDISK_WEB_TOOL)
    set(VDISK_WEB_TOOL ON)
endif()

if(VDISK_ENABLE)
    target_sources(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR}/vdisk.cc)
    target_include_directories(remapper PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(remapper PRIVATE
        VDISK_ENABLED
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
