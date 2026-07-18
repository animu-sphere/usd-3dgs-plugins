# Dual-mode root for a repository of OpenStrata plugin bundles.
#
# `ost` builds each bundle individually (`ost plugin build <bundle>`), composing
# the resolved runtime's environment and toolchain. This root exists so a plain
# CMake user can build the whole repo *without* `ost`:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<your-openusd-install>
#   cmake --build build
#
# It resolves OpenUSD once here, then add_subdirectory()s every bundle: either
# an immediate subdirectory of this root, or a subdirectory under plugins/.
# A bundle is any directory containing both an openstrata.plugin.yaml and a
# CMakeLists.txt. Each bundle guards its own `find_package(pxr)` with
# `if(NOT pxr_FOUND)`, so it builds either standalone (via `ost`) or as a
# subdirectory of this root. New bundles created with `ost plugin new` are picked
# up automatically — no edit here required.
cmake_minimum_required(VERSION 3.23)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
     _usd3dgs_version LIMIT_COUNT 1)
string(STRIP "${_usd3dgs_version}" _usd3dgs_version)

project(usd-3dgs-plugins
    VERSION ${_usd3dgs_version}
    DESCRIPTION "OpenUSD file-format plugins for 3D Gaussian Splatting"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USD3DGS_BUILD_TESTS "Build usd-3dgs plugin tests" ON)
if(USD3DGS_BUILD_TESTS)
    enable_testing()
endif()

# Workspace libraries are built before plugin bundles. OpenStrata executes the
# same edge from each bundle's requires.libraries declaration when a bundle is
# built standalone.
add_subdirectory("libs/gaussian-core")

if(NOT pxr_FOUND)
    find_package(pxr CONFIG REQUIRED)
endif()

# `CONFIGURE_DEPENDS` makes the build re-glob and reconfigure when a bundle is
# added or removed, so a later `cmake --build` picks up new `ost plugin new`
# bundles without a manual reconfigure.
set(_ost_bundle_roots "${CMAKE_CURRENT_SOURCE_DIR}")
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
    list(APPEND _ost_bundle_roots "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
endif()

foreach(_root ${_ost_bundle_roots})
    file(GLOB _entries LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE
         "${_root}" "${_root}/*")
    foreach(_entry ${_entries})
        set(_bundle "${_root}/${_entry}")
        if(IS_DIRECTORY "${_bundle}"
           AND EXISTS "${_bundle}/openstrata.plugin.yaml"
           AND EXISTS "${_bundle}/CMakeLists.txt")
            file(RELATIVE_PATH _rel_bundle "${CMAKE_CURRENT_SOURCE_DIR}" "${_bundle}")
            message(STATUS "OpenStrata: adding plugin bundle '${_rel_bundle}'")
            add_subdirectory("${_rel_bundle}")
        endif()
    endforeach()
endforeach()
