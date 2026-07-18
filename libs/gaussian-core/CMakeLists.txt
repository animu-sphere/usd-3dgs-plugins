# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _gaussian_core_version LIMIT_COUNT 1)
    string(STRIP "${_gaussian_core_version}" _gaussian_core_version)
else()
    set(_gaussian_core_version "0.1.0")
endif()

project(gaussianCore
    VERSION ${_gaussian_core_version}
    DESCRIPTION "Format-independent Gaussian cloud model and validation"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED GAUSSIANCORE_BUILD_TESTS)
    if(DEFINED USD3DGS_BUILD_TESTS)
        set(_gaussian_core_tests_default "${USD3DGS_BUILD_TESTS}")
    else()
        set(_gaussian_core_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(GAUSSIANCORE_BUILD_TESTS "Build gaussianCore tests"
           ${_gaussian_core_tests_default})
endif()

add_library(gaussianCore STATIC
    src/GaussianMath.cpp)
add_library(openstrata::gaussianCore ALIAS gaussianCore)
set_target_properties(gaussianCore PROPERTIES
    EXPORT_NAME gaussianCore
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(gaussianCore PUBLIC cxx_std_17)
target_include_directories(gaussianCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

if(MSVC)
    target_compile_options(gaussianCore PRIVATE /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS gaussianCore
    EXPORT gaussianCoreTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gaussianCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianCore")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT gaussianCoreTargets
    FILE gaussianCoreTargets.cmake
    NAMESPACE openstrata::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianCore")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/gaussianCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gaussianCore")

if(GAUSSIANCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
