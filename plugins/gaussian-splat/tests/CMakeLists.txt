# SPDX-License-Identifier: Apache-2.0

add_executable(gaussianSplatReader_tests test_gaussian_splat_reader.cpp)
target_link_libraries(gaussianSplatReader_tests PRIVATE
    gaussianSplatDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSplatReader_tests PRIVATE
    GAUSSIAN_SPLAT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSplatReader_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSplat_reader_unit COMMAND gaussianSplatReader_tests)

add_executable(gaussianSplatDecoder_tests test_gaussian_splat_decoder.cpp)
target_link_libraries(gaussianSplatDecoder_tests PRIVATE
    gaussianSplatDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSplatDecoder_tests PRIVATE
    GAUSSIAN_SPLAT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GAUSSIAN_SPLAT_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference")
if(MSVC)
    target_compile_options(gaussianSplatDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSplat_decoder_unit COMMAND gaussianSplatDecoder_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "gaussian-splat: Python3 not found; skipping integration test")
    return()
endif()

include("../../../cmake/Usd3dgsPythonTests.cmake")
usd3dgs_python_test_environment(_smoke_env gaussian-splat)

add_test(
    NAME gaussianSplat_python_smoke
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_splat_plugin.py")
set_tests_properties(gaussianSplat_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")
