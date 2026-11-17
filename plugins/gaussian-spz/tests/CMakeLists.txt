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
# but the check runs there too.
include("../../../cmake/Usd3dgsExportedSymbols.cmake")
usd3dgs_add_exported_symbol_test(
    NAME gaussianSpz_no_exported_decoder_symbols
    TARGET ${PLUGIN_NAME})

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
