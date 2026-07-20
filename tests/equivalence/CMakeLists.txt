# SPDX-License-Identifier: Apache-2.0
#
# Cross-format equivalence tests. This directory is deliberately outside both
# bundles: it links the PLY and SPZ decoders together, which no single bundle
# can do. `ost plugin build <bundle>` never configures it — only the plain
# root composition does, and only when both decoder targets are present.

add_executable(equivalence_tests test_equivalence.cpp)
target_link_libraries(equivalence_tests PRIVATE
    gaussianPlyDecoder
    gaussianSpzDecoder
    openstrata::gaussianCore)
target_compile_definitions(equivalence_tests PRIVATE
    EQUIVALENCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(equivalence_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussian_ply_spz_equivalence COMMAND equivalence_tests)
