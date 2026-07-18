# SPDX-License-Identifier: Apache-2.0
add_executable(gaussianCore_tests test_gaussian_math.cpp)
target_link_libraries(gaussianCore_tests PRIVATE openstrata::gaussianCore)
add_test(NAME gaussianCore_unit COMMAND gaussianCore_tests)
