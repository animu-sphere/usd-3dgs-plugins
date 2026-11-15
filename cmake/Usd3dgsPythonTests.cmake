# SPDX-License-Identifier: Apache-2.0
include_guard(GLOBAL)

# Usage: usd3dgs_python_test_environment(output_variable bundle...)
function(usd3dgs_python_test_environment output_variable)
    set(_env)
    if(pxr_DIR)
        # OpenStrata puts pxrConfig.cmake at the runtime root. SDKs can put
        # it under lib/cmake/pxr instead.
        set(_usd_root "${pxr_DIR}")
        if(NOT EXISTS "${pxr_DIR}/bin")
            get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
        endif()
        list(APPEND _env
            "PATH=path_list_prepend:${_usd_root}/bin"
            "PATH=path_list_prepend:${_usd_root}/lib"
            "PYTHONPATH=path_list_prepend:${_usd_root}/lib/python"
            "PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")
    endif()
    get_filename_component(_plugins "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../plugins" ABSOLUTE)
    foreach(_bundle IN LISTS ARGN)
        set(_bundle_dir "${_plugins}/${_bundle}")
        list(APPEND _env
            "PATH=path_list_prepend:${_bundle_dir}/lib"
            "PXR_PLUGINPATH_NAME=path_list_prepend:${_bundle_dir}/plugin/resources/${_bundle}")
    endforeach()
    set(${output_variable} "${_env}" PARENT_SCOPE)
endfunction()
