# SPDX-License-Identifier: Apache-2.0

add_executable(gaussianPlyDecoder_tests test_gaussian_ply_decoder.cpp)
target_link_libraries(gaussianPlyDecoder_tests PRIVATE
    gaussianPlyDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianPlyDecoder_tests PRIVATE
    GAUSSIAN_PLY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianPlyDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianPly_decoder_unit COMMAND gaussianPlyDecoder_tests)

# SuperSplat compressed PLY dialect, including the real-asset check of the
# corpus conversions against their lossless sources.
add_executable(gaussianPlyCompressedDecoder_tests test_compressed_ply_decoder.cpp)
target_link_libraries(gaussianPlyCompressedDecoder_tests PRIVATE
    gaussianPlyDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianPlyCompressedDecoder_tests PRIVATE
    GAUSSIAN_PLY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GAUSSIAN_PLY_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
if(MSVC)
    target_compile_options(gaussianPlyCompressedDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianPly_compressed_decoder_unit
    COMMAND gaussianPlyCompressedDecoder_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "gaussian-ply: Python3 not found; skipping integration test")
    return()
endif()

include("../../../cmake/Usd3dgsPythonTests.cmake")
usd3dgs_python_test_environment(_smoke_env gaussian-ply)

add_test(
    NAME gaussianPly_python_smoke
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_ply_plugin.py")
set_tests_properties(gaussianPly_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")
