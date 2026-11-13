# cmake -DSRC=<config-tool-web> -DDEMO=<json> -DDEMO2=<json> -DCSS=... -DBOOTSTRAP_JS=... -DOUT=... -P webtool.cmake
#
# Builds the single-file web config tool for the drive: the four scripts are
# joined (imports/exports removed), the two demo configs are added as the first
# examples, and the scripts and Bootstrap are put inline into index.html, so
# the page works without the internet. The page is then gzipped.

function(read_source name var)
    file(READ ${SRC}/${name} text)
    string(REPLACE "\r\n" "\n" text "${text}")
    set(${var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_unless_once haystack needle what)
    string(FIND "${haystack}" "${needle}" first)
    string(FIND "${haystack}" "${needle}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "webtool.cmake: expected ${what} exactly once")
    endif()
endfunction()

read_source(crc.js crc_js)
read_source(usages.js usages_js)
read_source(examples.js examples_js)
read_source(code.js code_js)
file(READ ${DEMO} demo)
file(READ ${DEMO2} demo2)

string(REPLACE "export default function" "function" crc_js "${crc_js}")
string(REGEX REPLACE "\nexport default [A-Za-z_]+;" "\n" usages_js "${usages_js}")
string(REGEX REPLACE "\nexport default [A-Za-z_]+;" "\n" examples_js "${examples_js}")
string(REGEX REPLACE "(^|\n)import [^\n]*" "\\1" code_js "${code_js}")

fail_unless_once("${examples_js}" "const examples = [" "'const examples = [' in examples.js")
string(REPLACE "const examples = [" "const examples = [\n{'description': 'RGB LED demo (WS2812 on GPIO16): Ctrl+Alt+F1-F5 modes, keys 1-0 colours, Scroll Lock cycles layers', 'config': ${demo}},
{'description': 'Logitech K400 / K400r / K400 Plus: Fn+Esc toggles Fn lock (needs the hidout module)', 'config': ${demo2}}," examples_js "${examples_js}")

set(app_js "${crc_js}\n${usages_js}\n${examples_js}\n${code_js}")
string(REGEX MATCH "(^|\n)(import|export) " leftover "${app_js}")
if(leftover)
    message(FATAL_ERROR "webtool.cmake: an import or export is left in the joined scripts")
endif()
# indentation and blank lines only; the scripts have no multi-line strings
string(REGEX REPLACE "\n[ \t]+" "\n" app_js "${app_js}")
string(REGEX REPLACE "\n\n+" "\n" app_js "${app_js}")

file(READ ${CSS} css)
file(READ ${BOOTSTRAP_JS} bootstrap_js)

foreach(var css bootstrap_js)
    string(REGEX REPLACE "/[*/]# sourceMappingURL=[^\n]*" "" ${var} "${${var}}")
endforeach()
foreach(var bootstrap_js app_js)
    string(REPLACE "</script" "<\\/script" ${var} "${${var}}")
endforeach()

read_source(index.html html)
string(REGEX REPLACE "\n[ \t]+" "\n" html "${html}")
string(REGEX REPLACE "\n\n+" "\n" html "${html}")

set(hidden_layer " d-none\">\n(<input [^\n]*(layer_checkbox|unmapped_passthrough_checkbox)[4-7][^0-9])")
string(REGEX MATCHALL "${hidden_layer}" found "${html}")
list(LENGTH found count)
if(NOT count EQUAL 8)
    message(FATAL_ERROR "webtool.cmake: expected 8 hidden layer 4-7 checkboxes in index.html, found ${count}")
endif()
string(REGEX REPLACE "${hidden_layer}" "\">\n\\1" html "${html}")

function(patch old new)
    string(REGEX MATCHALL "${old}" found "${html}")
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "webtool.cmake: expected one match for ${old} in index.html, found ${count}")
    endif()
    string(REGEX REPLACE "${old}" "${new}" html "${html}")
    set(html "${html}" PARENT_SCOPE)
endfunction()

patch("<div class=\"container d-flex flex-column\" style=\"max-width: 720px;" "<div class=\"container d-flex flex-column\" style=\"max-width: 900px;")
patch("<div class=\"row my-2 align-items-end\">" "<div class=\"row gx-2 my-2 align-items-end\">")
patch("<div class=\"row mb-1 mapping_container\">" "<div class=\"row gx-2 mb-1 mapping_container\">")
patch("<div class=\"col-3\">(\n<div class=\"d-flex align-items-end\">)" "<div class=\"col-4\">\\1")
patch("<div class=\"col-2 text-center\">Scaling" "<div class=\"col-1 text-center\">Scaling")
patch("<div class=\"col-3\">(\n<div class=\"d-flex lh-1 text-center\">)" "<div class=\"col-4\">\\1")
patch("flex-fill row gx-1 justify-content-center\"" "flex-fill row gx-1 flex-nowrap justify-content-center\"")
patch("<div class=\"row gx-1\">(\n<div class=\"col-auto ms-1\" title=\"Sticky\">)" "<div class=\"row gx-1 flex-nowrap\">\\1")
patch("<div class=\"col-2\"><input class=\"form-control scaling_input" "<div class=\"col-1\"><input class=\"form-control px-1 scaling_input")

function(swap pattern placeholder)
    string(REGEX MATCHALL "${pattern}" found "${html}")
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "webtool.cmake: expected one match for ${pattern} in index.html, found ${count}")
    endif()
    string(REGEX REPLACE "${pattern}" "${placeholder}" html "${html}")
    set(html "${html}" PARENT_SCOPE)
endfunction()

swap("<link href=\"[^\"]*bootstrap[^\"]*\\.css\"[^>]*>" "@VDISK_CSS@")
swap("<script src=\"[^\"]*bootstrap[^\"]*\\.js\"[^>]*></script>" "@VDISK_BOOTSTRAP_JS@")
swap("<script src=\"code\\.js\" type=\"module\"></script>" "@VDISK_APP_JS@")

string(REPLACE "@VDISK_CSS@" "<style>${css}</style>" html "${html}")
string(REPLACE "@VDISK_BOOTSTRAP_JS@" "<script>${bootstrap_js}</script>" html "${html}")
string(REPLACE "@VDISK_APP_JS@" "<script type=\"module\">${app_js}</script>" html "${html}")

if(CMAKE_VERSION VERSION_LESS 3.19)
    file(WRITE ${OUT} "${html}")
    return()
endif()

# CMake reads binary files only as hex; regroup it into base64 (3 bytes -> 4 characters)
function(base64_from_hex hex var)
    set(abc "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")
    string(LENGTH "${hex}" n)
    math(EXPR full "${n} / 6 * 6")
    string(SUBSTRING "${hex}" 0 ${full} body)
    string(SUBSTRING "${hex}" ${full} -1 tail)
    string(REGEX MATCHALL "......" groups "${body}")
    set(result "")
    set(chunk "")
    set(i 0)
    foreach(g IN LISTS groups)
        math(EXPR v "0x${g}")
        foreach(shift 18 12 6 0)
            math(EXPR k "(${v} >> ${shift}) & 63")
            string(SUBSTRING "${abc}" ${k} 1 c)
            string(APPEND chunk "${c}")
        endforeach()
        math(EXPR i "${i} + 1")
        if(i EQUAL 1000)
            string(APPEND result "${chunk}")
            set(chunk "")
            set(i 0)
        endif()
    endforeach()
    string(APPEND result "${chunk}")
    string(LENGTH "${tail}" t)
    if(t GREATER 0)
        if(t EQUAL 2)
            math(EXPR v "0x${tail}0000")
            set(shifts 18 12)
            set(pad "==")
        else()
            math(EXPR v "0x${tail}00")
            set(shifts 18 12 6)
            set(pad "=")
        endif()
        foreach(shift IN LISTS shifts)
            math(EXPR k "(${v} >> ${shift}) & 63")
            string(SUBSTRING "${abc}" ${k} 1 c)
            string(APPEND result "${c}")
        endforeach()
        string(APPEND result "${pad}")
    endif()
    set(${var} "${result}" PARENT_SCOPE)
endfunction()

# Gzip the page and wrap it in a loader that unpacks it with the browser's
# base64 decoder and DecompressionStream, then shows it.
file(WRITE ${OUT}.page.htm "${html}")
file(ARCHIVE_CREATE OUTPUT ${OUT}.page.htm.gz PATHS ${OUT}.page.htm FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
file(READ ${OUT}.page.htm.gz packed HEX)
base64_from_hex("${packed}" packed)
file(WRITE ${OUT} "<!doctype html>
<html><head><meta charset=\"utf-8\"><title>HID Remapper Configuration</title></head><body>
<script>
(async () => {
    const b64 = \"${packed}\";
    const bytes = Uint8Array.fromBase64 ? Uint8Array.fromBase64(b64) : Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const page = await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text();
    document.open();
    document.write(page);
    document.close();
})();
</script>
</body></html>
")
