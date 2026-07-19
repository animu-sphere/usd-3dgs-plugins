# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _gaussian_usd_version LIMIT_COUNT 1)
    string(STRIP "${_gaussian_usd_version}" _gaussian_usd_version)
else()
    set(_gaussian_usd_version "0.3.0")
endif()

project(gaussianUsd
    VERSION ${_gaussian_usd_version}
    DESCRIPTION "Shared GaussianCloudData to OpenUSD schema authoring"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED GAUSSIANUSD_BUILD_TESTS)
    if(DEFINED USD3DGS_BUILD_TESTS)
        set(_gaussian_usd_tests_default "${USD3DGS_BUILD_TESTS}")
    else()
        set(_gaussian_usd_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(GAUSSIANUSD_BUILD_TESTS "Build gaussianUsd tests"
           ${_gaussian_usd_tests_default})
endif()

# Resolved once by a composed root build; found here when this library is
# configured standalone.
if(NOT pxr_FOUND)
    find_package(pxr CONFIG REQUIRED)
endif()
if(NOT TARGET openstrata::gaussianCore)
    find_package(gaussianCore CONFIG REQUIRED)
endif()

add_library(gaussianUsd STATIC
    src/GaussianLayerWriter.cpp)
add_library(openstrata::gaussianUsd ALIAS gaussianUsd)
set_target_properties(gaussianUsd PROPERTIES
    EXPORT_NAME gaussianUsd
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(gaussianUsd PUBLIC cxx_std_17)
target_include_directories(gaussianUsd
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# OpenUSD ships either one monolithic target or per-component targets depending
# on how it was built. This mirrors the bundle-local openstrata_link_openusd
# resolution so the library builds against both layouts without depending on a
# plugin bundle's copy of that helper.
set(_gaussian_usd_components arch tf gf vt kind sdf usd usdGeom usdVol)
if(TARGET usd_ms)
    target_link_libraries(gaussianUsd PUBLIC usd_ms)
elseif(TARGET pxr::usd_ms)
    target_link_libraries(gaussianUsd PUBLIC pxr::usd_ms)
else()
    set(_gaussian_usd_targets)
    foreach(_component IN LISTS _gaussian_usd_components)
        if(TARGET ${_component})
            list(APPEND _gaussian_usd_targets ${_component})
        elseif(TARGET pxr::${_component})
            list(APPEND _gaussian_usd_targets pxr::${_component})
        endif()
    endforeach()
    if(_gaussian_usd_targets)
        target_link_libraries(gaussianUsd PUBLIC ${_gaussian_usd_targets})
    elseif(PXR_LIBRARIES)
        target_link_libraries(gaussianUsd PUBLIC ${PXR_LIBRARIES})
    else()
        message(FATAL_ERROR
            "Could not locate OpenUSD CMake targets or PXR_LIBRARIES for gaussianUsd.")
    endif()
endif()
target_include_directories(gaussianUsd PUBLIC ${PXR_INCLUDE_DIRS})
target_link_libraries(gaussianUsd PUBLIC openstrata::gaussianCore)

# /Brepro: MSVC stamps wall-clock times into object files and archive-member
# headers, so two builds of identical sources hash differently. The packaged
# archive is covered by the release digest-reproducibility gate and must not
# embed build time; lib.exe needs the flag separately from cl.exe.
if(MSVC)
    target_compile_definitions(gaussianUsd PRIVATE NOMINMAX)
    target_compile_options(gaussianUsd PRIVATE /utf-8 /Brepro)
    set_target_properties(gaussianUsd PROPERTIES STATIC_LIBRARY_OPTIONS /Brepro)
endif()

include(GNUInstallDirs)
install(TARGETS gaussianUsd
    EXPORT gaussianUsdTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gaussianUsdConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianUsdConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianUsd")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianUsdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT gaussianUsdTargets
    FILE gaussianUsdTargets.cmake
    NAMESPACE openstrata::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianUsd")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianUsdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianUsdConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianUsd")

if(GAUSSIANUSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
