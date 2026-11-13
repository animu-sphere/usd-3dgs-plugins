# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.23)
project(usd3dgs-package-consumer LANGUAGES CXX)

find_package(pxr CONFIG REQUIRED)

add_executable(open_stage open_stage.cpp)
target_compile_features(open_stage PRIVATE cxx_std_17)
set_target_properties(open_stage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

if(TARGET usd_ms)
    target_link_libraries(open_stage PRIVATE usd_ms)
elseif(TARGET pxr::usd_ms)
    target_link_libraries(open_stage PRIVATE pxr::usd_ms)
elseif(TARGET pxr::usdVol)
    target_link_libraries(open_stage PRIVATE pxr::usdVol)
else()
    target_link_libraries(open_stage PRIVATE usdVol)
endif()
