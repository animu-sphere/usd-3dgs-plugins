# SPDX-License-Identifier: Apache-2.0
#
# usd3dgs_add_exported_symbol_test(NAME <test> TARGET <plugin library>)
#
# Registers a CTest that fails when the plugin library exports a symbol of a
# vendored decoder (zstd, miniz, libwebp); check_exported_symbols.cmake holds
# the rule. Windows exports nothing from these C sources, but the check runs
# there too, so all three release targets assert the same thing.

set(_USD3DGS_EXPORTED_SYMBOLS_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/check_exported_symbols.cmake")

function(usd3dgs_add_exported_symbol_test)
    cmake_parse_arguments(ARG "" "NAME;TARGET" "" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_TARGET)
        message(FATAL_ERROR
            "usd3dgs_add_exported_symbol_test requires NAME and TARGET")
    endif()

    if(WIN32)
        get_filename_component(_linker_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(USD3DGS_DUMPBIN dumpbin HINTS "${_linker_dir}")
        set(_tool "${USD3DGS_DUMPBIN}")
        set(_format pe)
    elseif(APPLE)
        set(_tool "${CMAKE_NM}")
        set(_format macho)
    else()
        set(_tool "${CMAKE_NM}")
        set(_format elf)
    endif()

    if(NOT _tool)
        message(WARNING
            "${ARG_NAME}: no symbol listing tool found; the exported decoder "
            "symbol check is not registered")
        return()
    endif()

    add_test(
        NAME ${ARG_NAME}
        COMMAND "${CMAKE_COMMAND}"
            "-DLIBRARY=$<TARGET_FILE:${ARG_TARGET}>"
            "-DTOOL=${_tool}"
            "-DFORMAT=${_format}"
            -P "${_USD3DGS_EXPORTED_SYMBOLS_SCRIPT}")
endfunction()
