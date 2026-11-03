# SPDX-License-Identifier: Apache-2.0
#
# Three tiers, as in gaussian-ply and gaussian-spz: the container reader and the
# semantic decoder are unit tested without USD, and the Python smoke test pins
# the authored stage contract and the diagnostic catalog.

add_executable(gaussianSogReader_tests test_gaussian_sog_reader.cpp)
target_link_libraries(gaussianSogReader_tests PRIVATE
    gaussianSogDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSogReader_tests PRIVATE
    GAUSSIAN_SOG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSogReader_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSog_reader_unit COMMAND gaussianSogReader_tests)

add_executable(gaussianSogDecoder_tests test_gaussian_sog_decoder.cpp)
target_link_libraries(gaussianSogDecoder_tests PRIVATE
    gaussianSogDecoder
    openstrata::gaussianCore)
target_compile_definitions(gaussianSogDecoder_tests PRIVATE
    GAUSSIAN_SOG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSogDecoder_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSog_decoder_unit COMMAND gaussianSogDecoder_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "gaussian-sog: Python3 not found; skipping integration test")
    return()
endif()

set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_plugin_resources "${_bundle_dir}/plugin/resources/gaussian-sog")
set(_plugin_lib "${_bundle_dir}/lib")
set(_smoke_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:${_plugin_lib};PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_plugin_resources}")

add_test(
    NAME gaussianSog_python_smoke
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_sog_plugin.py")
set_tests_properties(gaussianSog_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")

# The asset-resolver matrix for the unbundled layout
# (docs/reference/RESOLVER_MATRIX.md): primary-asset spellings, anchored and
# search-path references, and companion failures through ArDefaultResolver.
add_test(
    NAME gaussianSog_python_resolver
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_sog_resolver.py")
set_tests_properties(gaussianSog_python_resolver PROPERTIES
    ENVIRONMENT_MODIFICATION "${_smoke_env}")

# The rows of the same matrix that ArDefaultResolver cannot reach: a URI-style
# primary identifier, companions served by a custom resolver, and an asset that
# serves a stream without GetBuffer(). A test-only `gstest:` URI resolver
# plugin serves them. It is built into its own directory beside a generated
# plugInfo.json, registered for this one test through PXR_PLUGINPATH_NAME,
# and never installed, so no other test or package sees it.
set(_uri_resolver_dir "${CMAKE_CURRENT_BINARY_DIR}/gstest-uri-resolver")
add_library(gsTestUriResolver SHARED uri_resolver/GsTestUriResolver.cpp)
target_compile_features(gsTestUriResolver PRIVATE cxx_std_17)
target_include_directories(gsTestUriResolver PRIVATE ${PXR_INCLUDE_DIRS})
set_target_properties(gsTestUriResolver PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${_uri_resolver_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${_uri_resolver_dir}"
    ARCHIVE_OUTPUT_DIRECTORY "${_uri_resolver_dir}")
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(gsTestUriResolver PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${_config} "${_uri_resolver_dir}"
        RUNTIME_OUTPUT_DIRECTORY_${_config} "${_uri_resolver_dir}"
        ARCHIVE_OUTPUT_DIRECTORY_${_config} "${_uri_resolver_dir}")
endforeach()
if(MSVC)
    target_compile_definitions(gsTestUriResolver PRIVATE NOMINMAX)
    target_compile_options(gsTestUriResolver PRIVATE /utf-8)
endif()
openstrata_link_openusd(
    TARGET gsTestUriResolver
    COMPONENTS arch tf ar)
set(GSTEST_URI_RESOLVER_LIBRARY
    "gsTestUriResolver${CMAKE_SHARED_LIBRARY_SUFFIX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/uri_resolver/plugInfo.json.in"
    "${_uri_resolver_dir}/plugInfo.json"
    @ONLY)

add_test(
    NAME gaussianSog_python_uri_resolver
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_gaussian_sog_uri_resolver.py")
set_tests_properties(gaussianSog_python_uri_resolver PROPERTIES
    ENVIRONMENT_MODIFICATION
        "${_smoke_env};PXR_PLUGINPATH_NAME=path_list_prepend:${_uri_resolver_dir};GSTEST_URI_RESOLVER_ROOT=set:${CMAKE_CURRENT_BINARY_DIR}/gstest-uri-store;GSTEST_URI_RESOLVER_LOG=set:${CMAKE_CURRENT_BINARY_DIR}/gstest-uri-resolver.log")
