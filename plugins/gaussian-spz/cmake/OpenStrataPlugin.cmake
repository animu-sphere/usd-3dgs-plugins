# SPDX-License-Identifier: Apache-2.0
# OpenStrata plugin scaffold helper, copied into generated bundles.
#
# Keep this file self-contained: generated projects must build with plain CMake
# and an OpenUSD SDK, without an OpenStrata source checkout or an `ost` binary.
include_guard(DIRECTORY)

set(OPENSTRATA_PLUGIN_CMAKE_HELPER_VERSION "1.1.0")

function(openstrata_default_build_type)
    get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endfunction()

function(openstrata_link_openusd)
    cmake_parse_arguments(ARG "" "TARGET;VISIBILITY" "COMPONENTS" ${ARGN})
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "openstrata_link_openusd requires TARGET")
    elseif(NOT TARGET ${ARG_TARGET})
        message(FATAL_ERROR
            "openstrata_link_openusd TARGET '${ARG_TARGET}' does not exist")
    endif()
    if(NOT ARG_COMPONENTS)
        message(FATAL_ERROR "openstrata_link_openusd requires COMPONENTS")
    endif()
    if(NOT ARG_VISIBILITY)
        set(ARG_VISIBILITY PRIVATE)
    elseif(NOT ARG_VISIBILITY MATCHES "^(PRIVATE|PUBLIC|INTERFACE)$")
        message(FATAL_ERROR
            "openstrata_link_openusd VISIBILITY must be PRIVATE, PUBLIC, or INTERFACE")
    endif()

    if(TARGET usd_ms)
        target_link_libraries(${ARG_TARGET} ${ARG_VISIBILITY} usd_ms)
        return()
    endif()
    if(TARGET pxr::usd_ms)
        target_link_libraries(${ARG_TARGET} ${ARG_VISIBILITY} pxr::usd_ms)
        return()
    endif()

    set(_pxr_targets)
    foreach(_component IN LISTS ARG_COMPONENTS)
        if(TARGET ${_component})
            list(APPEND _pxr_targets ${_component})
        elseif(TARGET pxr::${_component})
            list(APPEND _pxr_targets pxr::${_component})
        endif()
    endforeach()
    if(_pxr_targets)
        target_link_libraries(${ARG_TARGET} ${ARG_VISIBILITY} ${_pxr_targets})
    elseif(PXR_LIBRARIES)
        target_link_libraries(${ARG_TARGET} ${ARG_VISIBILITY} ${PXR_LIBRARIES})
    else()
        message(FATAL_ERROR
            "Could not locate OpenUSD CMake targets or PXR_LIBRARIES for ${ARG_TARGET}.")
    endif()
endfunction()

function(openstrata_configure_plugin)
    cmake_parse_arguments(ARG "" "TARGET;PLUG_INFO_INPUT;PLUG_INFO_OUTPUT" "" ${ARGN})
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "openstrata_configure_plugin requires TARGET")
    elseif(NOT TARGET ${ARG_TARGET})
        message(FATAL_ERROR
            "openstrata_configure_plugin TARGET '${ARG_TARGET}' does not exist")
    endif()
    if(NOT ARG_PLUG_INFO_INPUT OR NOT ARG_PLUG_INFO_OUTPUT)
        message(FATAL_ERROR
            "openstrata_configure_plugin requires PLUG_INFO_INPUT and PLUG_INFO_OUTPUT")
    endif()

    # USD resolves LibraryPath relative to plugInfo.json, so stage the shared
    # library into this bundle's lib/ for every generator configuration. Use
    # CMAKE_CURRENT_SOURCE_DIR (this bundle), not CMAKE_SOURCE_DIR — when the
    # bundle is consumed via add_subdirectory() the latter points at the parent
    # workspace root and would stage the lib away from the committed
    # plugInfo.json LibraryPath.
    set(OPENSTRATA_PLUGIN_LIBRARY_PREFIX "lib")
    set_target_properties(${ARG_TARGET} PROPERTIES
        PREFIX "${OPENSTRATA_PLUGIN_LIBRARY_PREFIX}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${ARG_TARGET} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_${_config} "${CMAKE_CURRENT_SOURCE_DIR}/lib"
            LIBRARY_OUTPUT_DIRECTORY_${_config} "${CMAKE_CURRENT_SOURCE_DIR}/lib"
            RUNTIME_OUTPUT_DIRECTORY_${_config} "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    endforeach()

    if(MSVC)
        target_compile_definitions(${ARG_TARGET} PRIVATE NOMINMAX)
        target_compile_options(${ARG_TARGET} PRIVATE /utf-8)
    endif()

    configure_file("${ARG_PLUG_INFO_INPUT}" "${ARG_PLUG_INFO_OUTPUT}" @ONLY)
endfunction()

function(openstrata_install_plugin_bundle)
    cmake_parse_arguments(
        ARG
        ""
        "TARGET;RESOURCE_DESTINATION;FIXTURE_SOURCE;EXPORT_SET"
        "MANIFESTS;RESOURCES"
        ${ARGN})
    if(NOT ARG_TARGET)
        message(FATAL_ERROR "openstrata_install_plugin_bundle requires TARGET")
    elseif(NOT TARGET ${ARG_TARGET})
        message(FATAL_ERROR
            "openstrata_install_plugin_bundle TARGET '${ARG_TARGET}' does not exist")
    endif()
    if(NOT ARG_RESOURCES OR NOT ARG_RESOURCE_DESTINATION)
        message(FATAL_ERROR
            "openstrata_install_plugin_bundle requires RESOURCES and RESOURCE_DESTINATION")
    endif()

    if(ARG_EXPORT_SET)
        install(TARGETS ${ARG_TARGET}
            EXPORT ${ARG_EXPORT_SET}
            RUNTIME DESTINATION "lib"
            LIBRARY DESTINATION "lib"
            ARCHIVE DESTINATION "lib")
    else()
        install(TARGETS ${ARG_TARGET}
            RUNTIME DESTINATION "lib"
            LIBRARY DESTINATION "lib"
            ARCHIVE DESTINATION "lib")
    endif()
    install(FILES ${ARG_RESOURCES} DESTINATION "${ARG_RESOURCE_DESTINATION}")
    if(ARG_MANIFESTS)
        install(FILES ${ARG_MANIFESTS} DESTINATION ".")
    endif()
    if(ARG_FIXTURE_SOURCE)
        install(DIRECTORY "${ARG_FIXTURE_SOURCE}/" DESTINATION "tests/fixtures")
    endif()
endfunction()
