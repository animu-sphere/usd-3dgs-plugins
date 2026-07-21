# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

project(GaussianSogPlugin
    VERSION 0.5.0
    DESCRIPTION "OpenUSD sog file-format plugin (v0.5.0 decoder skeleton)"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenStrataPlugin.cmake")
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

set(PLUGIN_NAME GaussianSogFileFormat)

add_library(${PLUGIN_NAME} SHARED
    src/GaussianSogFileFormat.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    openstrata::gaussianCore)
# /Brepro: MSVC embeds wall-clock timestamps in object files, the PE header,
# and the debug directory, so two builds of identical sources produce
# different bytes. The release lane gates on package digest reproducibility;
# /Brepro replaces those stamps with content hashes.
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /utf-8 /Brepro)
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
    COMPONENTS arch tf gf vt sdf usd)

# The copied helper keeps output/install mechanics identical in standalone and
# add_subdirectory workspace builds. This file still owns the target, OpenUSD
# components, resource path, and fixtures as the bundle's readable policy.
openstrata_configure_plugin(
    TARGET ${PLUGIN_NAME}
    PLUG_INFO_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-sog/plugInfo.json.in"
    PLUG_INFO_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-sog/plugInfo.json")

openstrata_install_plugin_bundle(
    TARGET ${PLUGIN_NAME}
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-sog/plugInfo.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/gaussian-sog/diagnostics.json"
    RESOURCE_DESTINATION "plugin/resources/gaussian-sog"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.plugin.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.scaffold.yaml"
    FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    DESTINATION "share/doc/usd-3dgs-plugins"
    OPTIONAL)

if(USD3DGS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
