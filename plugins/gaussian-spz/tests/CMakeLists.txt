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
    GAUSSIAN_SPZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSpzDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSpz_decoder_unit COMMAND gaussianSpzDecoder_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "gaussian-spz: Python3 not found; skipping integration test")
    return()
endif()

set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_plugin_resources "${_bundle_dir}/plugin/resources/gaussian-spz")
set(_plugin_lib "${_bundle_dir}/lib")
set(_smoke_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:${_plugin_lib};PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_plugin_resources}")

add_test(
    NAME gaussianSpz_python_smoke
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_spz_plugin.py")
set_tests_properties(gaussianSpz_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")
