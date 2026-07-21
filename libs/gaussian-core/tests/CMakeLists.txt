# SPDX-License-Identifier: Apache-2.0
add_executable(gaussianCore_tests test_gaussian_math.cpp)
target_link_libraries(gaussianCore_tests PRIVATE openstrata::gaussianCore)
add_test(NAME gaussianCore_unit COMMAND gaussianCore_tests)

# The decoder test kit and its mock-decoder demonstration: a decoder written
# against GAUSSIAN_MODEL_CONTRACT.md alone, deliberately linking no bundle
# code.
add_executable(gaussianCore_decoder_kit_tests test_decoder_kit.cpp)
target_link_libraries(gaussianCore_decoder_kit_tests
    PRIVATE openstrata::gaussianCore)
add_test(NAME gaussianCore_decoder_kit COMMAND gaussianCore_decoder_kit_tests)
