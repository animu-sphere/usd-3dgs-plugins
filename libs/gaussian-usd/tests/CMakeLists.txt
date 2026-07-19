# SPDX-License-Identifier: Apache-2.0
add_executable(gaussianUsd_tests test_gaussian_layer_writer.cpp)
target_link_libraries(gaussianUsd_tests PRIVATE openstrata::gaussianUsd)
if(MSVC)
    target_compile_definitions(gaussianUsd_tests PRIVATE NOMINMAX)
    target_compile_options(gaussianUsd_tests PRIVATE /utf-8)
endif()
add_test(NAME gaussianUsd_writer_unit COMMAND gaussianUsd_tests)

# Unlike gaussianCore, this test links OpenUSD, so the runtime loader needs the
# SDK's shared libraries and its plugin resources (the usdVol schema that
# ParticleField3DGaussianSplat is defined in) on the test's search paths.
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()
if(_usd_root)
    set_tests_properties(gaussianUsd_writer_unit PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib")
else()
    # Without this the test fails at load time with a bare "the application
    # could not start" and no indication that the cause is a build-time path
    # heuristic rather than the test itself.
    message(WARNING
        "gaussianUsd: could not derive the OpenUSD root from pxr_DIR "
        "('${pxr_DIR}'), so gaussianUsd_writer_unit runs without the SDK on "
        "its library and plugin search paths and will likely fail to load.")
endif()
