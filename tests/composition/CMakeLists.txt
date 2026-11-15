# SPDX-License-Identifier: Apache-2.0
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "USD composition: Python3 not found; skipping")
    return()
endif()

include("../../cmake/Usd3dgsPythonTests.cmake")
usd3dgs_python_test_environment(_test_env gaussian-ply gaussian-spz gaussian-sog)

add_test(
    NAME gaussian_usd_composition
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_composition.py")
set_tests_properties(gaussian_usd_composition PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_env}")
