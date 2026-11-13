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
        set(VDISK_TOOL_SRC ${CMAKE_CURRENT_LIST_DIR}/../../config-tool-web)
        set(VDISK_DEMO ${CMAKE_CURRENT_LIST_DIR}/../ws2812/ws2812_example.json)
        set(VDISK_TOOL_HTML ${CMAKE_CURRENT_BINARY_DIR}/vdisk_webtool.htm)

        # Bootstrap: the same files and hashes as the CDN links in index.html
        set(VDISK_BOOTSTRAP_URL https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist)
        set(VDISK_BOOTSTRAP_CSS_SHA384 d41984e24581abbf2262116576f2ae85f4c053a6ae53cb53f785ab1dfb630dbac2117494d68068ab297642f67a8c85b7)
        set(VDISK_BOOTSTRAP_JS_SHA384 4091edbc6866afd5ce22923a615bad1bed9038af53f999cde24cc537546d2b7cc414422cc619665a5e7f6044afa59d77)
        file(READ ${VDISK_TOOL_SRC}/index.html VDISK_INDEX)
        foreach(f css/bootstrap.min.css js/bootstrap.min.js)
            string(FIND "${VDISK_INDEX}" "${VDISK_BOOTSTRAP_URL}/${f}" found)
            if(found EQUAL -1)
                message(FATAL_ERROR "vdisk: config-tool-web/index.html no longer loads ${VDISK_BOOTSTRAP_URL}/${f}. Update the Bootstrap URL and hashes in vdisk.cmake, or build with -DVDISK_WEB_TOOL=OFF.")
            endif()
        endforeach()
        file(DOWNLOAD ${VDISK_BOOTSTRAP_URL}/css/bootstrap.min.css ${CMAKE_CURRENT_BINARY_DIR}/vdisk_bootstrap.min.css
            EXPECTED_HASH SHA384=${VDISK_BOOTSTRAP_CSS_SHA384})
        file(DOWNLOAD ${VDISK_BOOTSTRAP_URL}/js/bootstrap.min.js ${CMAKE_CURRENT_BINARY_DIR}/vdisk_bootstrap.min.js
            EXPECTED_HASH SHA384=${VDISK_BOOTSTRAP_JS_SHA384})

        # jsmin.c (https://github.com/douglascrockford/JSMin, commit 75b6ccc,
        # unmodified) is built first for the build machine, like the SDK builds
        # pioasm, and then used below to make the web tool file.
        include(ExternalProject)
        set(VDISK_JSMIN_SRC ${CMAKE_CURRENT_BINARY_DIR}/vdisk_jsmin_src)
        file(WRITE ${VDISK_JSMIN_SRC}/CMakeLists.txt
"cmake_minimum_required(VERSION 3.13)
project(jsmin C)
add_executable(jsmin ${CMAKE_CURRENT_LIST_DIR}/jsmin.c)
install(TARGETS jsmin DESTINATION .)
")
        set(VDISK_JSMIN_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/vdisk_jsmin_install)
        ExternalProject_Add(vdiskJsminBuild
            PREFIX vdisk_jsmin
            SOURCE_DIR ${VDISK_JSMIN_SRC}
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/vdisk_jsmin
            INSTALL_DIR ${VDISK_JSMIN_INSTALL}
            CMAKE_ARGS
                "--no-warn-unused-cli"
                "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
                "-DCMAKE_INSTALL_PREFIX=${VDISK_JSMIN_INSTALL}"
                "-DCMAKE_RULE_MESSAGES=OFF"
                "-DCMAKE_INSTALL_MESSAGE=NEVER"
            BUILD_BYPRODUCTS ${VDISK_JSMIN_INSTALL}/jsmin${CMAKE_HOST_EXECUTABLE_SUFFIX}
            BUILD_ALWAYS 1
        )
        if(CMAKE_HOST_WIN32)
            set(VDISK_JSMIN ${VDISK_JSMIN_INSTALL}/jsmin.exe)
        else()
            set(VDISK_JSMIN ${VDISK_JSMIN_INSTALL}/jsmin)
        endif()

        add_custom_command(
            OUTPUT ${VDISK_TOOL_HTML}
            COMMAND ${CMAKE_COMMAND}
                -DSRC=${VDISK_TOOL_SRC}
                -DDEMO=${VDISK_DEMO}
                -DJSMIN=${VDISK_JSMIN}
                -DCSS=${CMAKE_CURRENT_BINARY_DIR}/vdisk_bootstrap.min.css
                -DBOOTSTRAP_JS=${CMAKE_CURRENT_BINARY_DIR}/vdisk_bootstrap.min.js
                -DOUT=${VDISK_TOOL_HTML}
                -P ${CMAKE_CURRENT_LIST_DIR}/webtool.cmake
            DEPENDS
                vdiskJsminBuild
                ${CMAKE_CURRENT_LIST_DIR}/webtool.cmake
                ${VDISK_DEMO}
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
