# SPDX-License-Identifier: Apache-2.0

add_executable(gaussianSpzReader_tests test_gaussian_spz_reader.cpp)
target_link_libraries(gaussianSpzReader_tests PRIVATE
    gaussianSpzDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSpzReader_tests PRIVATE
    GAUSSIAN_SPZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSpzReader_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSpz_reader_unit COMMAND gaussianSpzReader_tests)

add_executable(gaussianSpzDecoder_tests test_gaussian_spz_decoder.cpp)
target_link_libraries(gaussianSpzDecoder_tests PRIVATE
    gaussianSpzDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSpzDecoder_tests PRIVATE
    GAUSSIAN_SPZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GAUSSIAN_SPZ_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
if(MSVC)
    target_compile_options(gaussianSpzDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSpz_decoder_unit COMMAND gaussianSpzDecoder_tests)

# The vendored zstd and miniz decoders must stay internal to the plugin
# library (ADR 0002 item 5). Windows exports nothing without ZSTD_DLL_EXPORT,
# but the check runs there too, so all three release targets assert the same
# thing.
if(WIN32)
    get_filename_component(_linker_dir "${CMAKE_LINKER}" DIRECTORY)
    find_program(GAUSSIAN_SPZ_DUMPBIN dumpbin HINTS "${_linker_dir}")
    set(_symbol_tool "${GAUSSIAN_SPZ_DUMPBIN}")
    set(_symbol_format pe)
elseif(APPLE)
    set(_symbol_tool "${CMAKE_NM}")
    set(_symbol_format macho)
else()
    set(_symbol_tool "${CMAKE_NM}")
    set(_symbol_format elf)
endif()
if(_symbol_tool)
    add_test(
        NAME gaussianSpz_no_exported_decoder_symbols
        COMMAND "${CMAKE_COMMAND}"
            "-DLIBRARY=$<TARGET_FILE:${PLUGIN_NAME}>"
            "-DTOOL=${_symbol_tool}"
            "-DFORMAT=${_symbol_format}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_exported_symbols.cmake")
else()
    message(WARNING
        "gaussian-spz: no symbol listing tool found; the exported decoder "
        "symbol check is not registered")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "gaussian-spz: Python3 not found; skipping integration test")
    return()
endif()

include("../../../cmake/Usd3dgsPythonTests.cmake")
usd3dgs_python_test_environment(_smoke_env gaussian-spz)

add_test(
    NAME gaussianSpz_python_smoke
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_spz_plugin.py")
set_tests_properties(gaussianSpz_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")
