# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

# C is enabled alongside C++ for the vendored libwebp and miniz translation units.
project(GaussianSogPlugin
    VERSION 0.5.0
    DESCRIPTION "OpenUSD sog file-format plugin"
    LANGUAGES C CXX)

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
if(NOT TARGET openstrata::gaussianUsd)
    find_package(gaussianUsd CONFIG REQUIRED)
endif()

# Vendored libwebp decoder subset (third_party/libwebp, see its VENDORING.md).
# Built as one static library the SOG reader links for lossless-WebP (VP8L)
# property-plane decoding; the ZIP container, meta.json, and every
# Gaussian-specific dequantization stay in this repository. The subset self-
# selects its SIMD paths per target, so no architecture flag is needed here.
set(LIBWEBP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libwebp")
if(NOT EXISTS "${LIBWEBP_ROOT}/src/webp/decode.h")
    message(FATAL_ERROR "Vendored libwebp was not found at ${LIBWEBP_ROOT}")
endif()
file(GLOB_RECURSE LIBWEBP_SOURCES CONFIGURE_DEPENDS "${LIBWEBP_ROOT}/src/*.c")
add_library(gaussianSogWebpDecoder STATIC ${LIBWEBP_SOURCES})
set_target_properties(gaussianSogWebpDecoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
# libwebp's own sources are root-relative (`src/dec/...`); consumers use the
# public `webp/decode.h` (whose nested include is relative), so they only need
# the `src` directory.
target_include_directories(gaussianSogWebpDecoder
    PRIVATE "${LIBWEBP_ROOT}"
    INTERFACE "${LIBWEBP_ROOT}/src")
# Vendored third-party C: silence its warnings so a strict bundle build (e.g.
# /WX) is not held hostage to upstream style, and keep MSVC builds reproducible.
if(MSVC)
    target_compile_options(gaussianSogWebpDecoder PRIVATE /w /Brepro)
else()
    target_compile_options(gaussianSogWebpDecoder PRIVATE -w)
endif()

# Container reader and semantic decoder are deliberately USD-free and shared by
# the plugin and unit tests, mirroring the PLY and SPZ reader/decoder split.
# miniz is isolated behind SogReader, this time with its archive-reading API
# enabled for the bundled `.sog` ZIP; libwebp is isolated behind the same class
# for lossless property planes. Every Gaussian-specific decision — meta.json
# schema, codebooks, dequantization — stays in this repository, so container
# diagnostics keep their required granularity (SOG_FORMAT.md §2).
set(MINIZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/miniz")
if(NOT EXISTS "${MINIZ_ROOT}/miniz.h")
    message(FATAL_ERROR "Vendored miniz was not found at ${MINIZ_ROOT}")
endif()

add_library(gaussianSogDecoder STATIC
    src/io/SogJson.cpp
    src/io/SogReader.cpp
    src/io/GaussianSogDecoder.cpp
    "${MINIZ_ROOT}/miniz.c")
set_target_properties(gaussianSogDecoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(gaussianSogDecoder PUBLIC cxx_std_17)
target_include_directories(gaussianSogDecoder
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${MINIZ_ROOT}")
# The bundled layout needs miniz's archive *reading* API, which gaussian-spz
# compiles out (MINIZ_NO_ARCHIVE_APIS) because SPZ is a bare gzip member. The
# writing, stdio, and time facilities stay off here too: no archive is ever
# produced, and keeping wall-clock and filesystem access out of the decode path
# is what the reproducible-package gate depends on.
target_compile_definitions(gaussianSogDecoder PRIVATE
    MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_STDIO
    MINIZ_NO_TIME)
target_link_libraries(gaussianSogDecoder
    PUBLIC openstrata::gaussianCore
    PRIVATE gaussianSogWebpDecoder)
# /Brepro here and on the shared library below: MSVC embeds wall-clock
# timestamps in object files, the PE header, and the debug directory, so two
# builds of identical sources produce different bytes. The release lane gates
# on package digest reproducibility; /Brepro replaces those stamps with
# content hashes.
if(MSVC)
    target_compile_options(gaussianSogDecoder PRIVATE /utf-8 /Brepro)
endif()

set(PLUGIN_NAME GaussianSogFileFormat)

add_library(${PLUGIN_NAME} SHARED
    src/GaussianSogFileFormat.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    gaussianSogDecoder
    openstrata::gaussianCore
    openstrata::gaussianUsd)
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
    COMPONENTS arch tf gf vt kind sdf ar usd usdGeom usdVol)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/../../THIRD_PARTY_NOTICES.md"
    DESTINATION "share/doc/usd-3dgs-plugins"
    OPTIONAL)

if(USD3DGS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
