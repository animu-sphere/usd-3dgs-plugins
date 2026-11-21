# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

project(GaussianSplatPlugin
    VERSION 0.13.0
    DESCRIPTION "OpenUSD splat file-format plugin"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("../../cmake/OpenStrataPlugin.cmake")
openstrata_default_build_type()

if(NOT DEFINED USD3DGS_BUILD_TESTS)
    option(USD3DGS_BUILD_TESTS "Build usd-3dgs plugin tests" ${PROJECT_IS_TOP_LEVEL})
endif()

# Resolved from the runtime OpenStrata activates (toolchain sets CMAKE_PREFIX_PATH).
# Guarded so a parent project (a dual-mode root that `add_subdirectory()`s this
# bundle) can resolve OpenUSD once and have it flow down here.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()

# OpenStrata builds and installs this manifest-declared plain-library closure
# before configuring the bundle. A composed root build already owns the alias.
if(NOT TARGET openstrata::gaussianCore)
    find_package(gaussianCore CONFIG REQUIRED)
endif()
if(NOT TARGET openstrata::gaussianUsd)
    find_package(gaussianUsd CONFIG REQUIRED)
endif()

# File layout reader and semantic decoder are deliberately USD-free and shared
# by the plugin and unit tests, mirroring the other bundles' reader/decoder
# split. `.splat` needs no third-party code.
add_library(gaussianSplatDecoder STATIC
    src/io/SplatReader.cpp
    src/io/GaussianSplatDecoder.cpp)
set_target_properties(gaussianSplatDecoder PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(gaussianSplatDecoder PUBLIC cxx_std_17)
target_include_directories(gaussianSplatDecoder
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(gaussianSplatDecoder PUBLIC openstrata::gaussianCore)
# /Brepro here and on the shared library below: MSVC embeds wall-clock
# timestamps in object files, the PE header, and the debug directory, so two
# builds of identical sources produce different bytes. The release lane gates
# on package digest reproducibility; /Brepro replaces those stamps with
# content hashes.
if(MSVC)
    target_compile_options(gaussianSplatDecoder PRIVATE /utf-8 /Brepro)
endif()

set(PLUGIN_NAME GaussianSplatFileFormat)

add_library(${PLUGIN_NAME} SHARED
    src/GaussianSplatFileFormat.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    gaussianSplatDecoder
    openstrata::gaussianCore
    openstrata::gaussianUsd)
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /Brepro)
    target_link_options(${PLUGIN_NAME} PRIVATE /Brepro)
endif()

# `ost plugin build` writes this fragment when the bundle co-hosts a compiled
# schema (`schema.usda` + `provides: usd-schema:*`). It adds the generated typed
# API sources to this same shared library and defines the generated `*_EXPORTS`
# macro so the schema symbols are exported from the plugin binary. Plain
# file-format bundles have no fragment and stay unchanged.
if(DEFINED OPENSTRATA_SCHEMA_SOURCES_FILE AND EXISTS "${OPENSTRATA_SCHEMA_SOURCES_FILE}")
    include("${OPENSTRATA_SCHEMA_SOURCES_FILE}")
endif()

openstrata_link_openusd(
    TARGET ${PLUGIN_NAME}
    COMPONENTS arch tf gf vt kind sdf usd usdGeom usdVol)

openstrata_configure_plugin(
    TARGET ${PLUGIN_NAME}
    PLUG_INFO_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-splat/plugInfo.json.in"
    PLUG_INFO_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-splat/plugInfo.json")

openstrata_install_plugin_bundle(
    TARGET ${PLUGIN_NAME}
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-splat/plugInfo.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-splat/diagnostics.json"
    RESOURCE_DESTINATION "plugin/resources/gaussian-splat"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.plugin.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.scaffold.yaml"
    FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../THIRD_PARTY_NOTICES.md"
    DESTINATION "share/doc/usd-3dgs-plugins"
    OPTIONAL)

if(USD3DGS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
