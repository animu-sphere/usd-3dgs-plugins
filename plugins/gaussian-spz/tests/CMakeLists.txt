# SPDX-License-Identifier: Apache-2.0

add_executable(gaussianSpzReader_tests test_gaussian_spz_reader.cpp)
target_link_libraries(gaussianSpzReader_tests PRIVATE
    gaussianSpzReader
    openstrata::gaussianCore)
target_compile_definitions(gaussianSpzReader_tests PRIVATE
    GAUSSIAN_SPZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(MSVC)
    target_compile_options(gaussianSpzReader_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianSpz_reader_unit COMMAND gaussianSpzReader_tests)

# The Python/OpenUSD smoke test arrives with the USD integration workstream:
# in this development snapshot the plugin's Read() deliberately fails after
# container validation, so there is no stage to open yet.
