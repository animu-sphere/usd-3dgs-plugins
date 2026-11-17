# SPDX-License-Identifier: Apache-2.0
#
# Asserts that the plugin library exports no symbol of its vendored
# decoders, zstd (ADR 0002 item 5) and miniz. A USD host often has another
# libzstd loaded, and the gaussian-sog plugin carries its own miniz built with
# other definitions; on ELF, an exported symbol could be interposed by either
# and mix two copies' internal structures. v0.10.0 exported 40 miniz symbols
# from the Linux library; hidden C visibility removed them.
#
#   cmake -DLIBRARY=<file> -DTOOL=<nm|dumpbin> -DFORMAT=<elf|macho|pe>
#         -P check_exported_symbols.cmake

foreach(_required LIBRARY TOOL FORMAT)
    if(NOT ${_required})
        message(FATAL_ERROR "check_exported_symbols: ${_required} is not set")
    endif()
endforeach()
if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "check_exported_symbols: ${LIBRARY} does not exist")
endif()

if(FORMAT STREQUAL "elf")
    set(_command "${TOOL}" -D --defined-only "${LIBRARY}")
elseif(FORMAT STREQUAL "macho")
    set(_command "${TOOL}" -g -U "${LIBRARY}")
elseif(FORMAT STREQUAL "pe")
    set(_command "${TOOL}" /nologo /exports "${LIBRARY}")
else()
    message(FATAL_ERROR "check_exported_symbols: unknown FORMAT ${FORMAT}")
endif()

execute_process(
    COMMAND ${_command}
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _errors
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "check_exported_symbols: '${_command}' failed (${_result}): ${_errors}")
endif()

# The symbol name is the last field on each line of all three listings.
# Mach-O names carry a leading underscore.
string(REPLACE "\r" "" _output "${_output}")
string(REPLACE "\n" ";" _lines "${_output}")
set(_leaked "")
set(_symbols 0)
foreach(_line IN LISTS _lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "")
        continue()
    endif()
    string(REGEX REPLACE "^.*[ \t]" "" _name "${_line}")
    math(EXPR _symbols "${_symbols} + 1")
    if(_name MATCHES "^_?(ZSTD_|ZSTDv|HUF_|FSE_|ERR_|XXH|HIST_|POOL_|mz_|tinfl_|tdefl_)")
        list(APPEND _leaked "${_name}")
    endif()
endforeach()

if(_leaked)
    list(LENGTH _leaked _count)
    list(JOIN _leaked "\n  " _listing)
    message(FATAL_ERROR
        "${LIBRARY} exports ${_count} vendored decoder symbol(s):\n  ${_listing}")
endif()
message(STATUS
    "${LIBRARY}: no vendored decoder symbol among ${_symbols} listing line(s)")
