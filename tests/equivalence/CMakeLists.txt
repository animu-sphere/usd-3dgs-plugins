# SPDX-License-Identifier: Apache-2.0
#
# Cross-format equivalence tests. This directory is deliberately outside every
# bundle: it links more than one bundle's decoder, which no single bundle can
# do. `ost plugin build <bundle>` never configures it — only the plain root
# composition does, and only when all three decoder targets are present.
#
# One executable per quantizing format, both comparing against the lossless PLY
# decoder: gaussianSpzDecoder and gaussianSogDecoder each carry their own
# vendored miniz translation unit (with different definitions, since SPZ
# compiles the archive APIs out and SOG needs them), so linking both into one
# program is a duplicate-symbol error. See equivalence_common.h.

foreach(_suite
        "equivalence_tests;test_equivalence.cpp;gaussianSpzDecoder;gaussian_ply_spz_equivalence"
        "sog_equivalence_tests;test_sog_equivalence.cpp;gaussianSogDecoder;gaussian_ply_sog_equivalence")
    list(GET _suite 0 _target)
    list(GET _suite 1 _source)
    list(GET _suite 2 _decoder)
    list(GET _suite 3 _test_name)

    add_executable(${_target} "${_source}")
    target_link_libraries(${_target} PRIVATE
        gaussianPlyDecoder
        ${_decoder}
        openstrata::gaussianCore)
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${_target} PRIVATE
        EQUIVALENCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    if(MSVC)
        target_compile_options(${_target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${_test_name} COMMAND ${_target})
endforeach()
