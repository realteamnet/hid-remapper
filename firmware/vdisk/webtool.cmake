# cmake -DSRC=<config-tool-web> -DDEMO=<json> -DJSMIN=<jsmin> -DCSS=... -DBOOTSTRAP_JS=... -DOUT=... -P webtool.cmake
#
# Builds the single-file web config tool for the drive: the four scripts are
# joined (imports/exports removed), the demo config is added as the first
# example, the result is minified with jsmin, and the scripts and Bootstrap
# are put inline into index.html, so the page works without the internet.

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

string(REPLACE "export default function" "function" crc_js "${crc_js}")
string(REGEX REPLACE "\nexport default [A-Za-z_]+;" "\n" usages_js "${usages_js}")
string(REGEX REPLACE "\nexport default [A-Za-z_]+;" "\n" examples_js "${examples_js}")
string(REGEX REPLACE "(^|\n)import [^\n]*" "\\1" code_js "${code_js}")

fail_unless_once("${examples_js}" "const examples = [" "'const examples = [' in examples.js")
string(REPLACE "const examples = [" "const examples = [\n{'description': 'RGB LED demo (WS2812 on GPIO16): F1-F5 modes, keys 1-0 colours, Scroll Lock cycles layers', 'config': ${demo}}," examples_js "${examples_js}")

set(app_js "${crc_js}\n${usages_js}\n${examples_js}\n${code_js}")
string(REGEX MATCH "(^|\n)(import|export) " leftover "${app_js}")
if(leftover)
    message(FATAL_ERROR "webtool.cmake: an import or export is left in the joined scripts")
endif()

file(WRITE ${OUT}.js "${app_js}")
execute_process(COMMAND ${JSMIN} INPUT_FILE ${OUT}.js OUTPUT_FILE ${OUT}.min.js RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "webtool.cmake: jsmin failed (${result})")
endif()
file(READ ${OUT}.min.js app_js)
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

# Gzip the page and wrap it in a loader that unpacks it with the browser's
# DecompressionStream and shows it.
file(WRITE ${OUT}.page.htm "${html}")
file(ARCHIVE_CREATE OUTPUT ${OUT}.page.htm.gz PATHS ${OUT}.page.htm FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
file(READ ${OUT}.page.htm.gz packed HEX)
file(WRITE ${OUT} "<!doctype html>
<html><head><meta charset=\"utf-8\"><title>HID Remapper Configuration</title></head><body>
<script>
(async () => {
    const hex = \"${packed}\";
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) {
        bytes[i] = parseInt(hex.slice(2 * i, 2 * i + 2), 16);
    }
    const page = await new Response(new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))).text();
    document.open();
    document.write(page);
    document.close();
})();
</script>
</body></html>
")
