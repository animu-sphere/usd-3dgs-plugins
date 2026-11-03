# SPDX-License-Identifier: Apache-2.0
#
# The large-asset benchmark corpus (docs/reference/BENCHMARK_CORPUS.md). Like
# tests/equivalence, this directory sits outside every bundle: one suite opens
# all three formats, so it needs every plugin registered at once, which only
# the plain root composition provides.
#
# Two tests:
#
# * benchmark_corpus_manifest needs only Python. It checks corpus.json against
#   the committed assets and the scale tier's WebP encoder against the SOG
#   fixture generator's reference encoder.
# * benchmark_corpus_ci generates the `ci: true` rows of the scale tier (the
#   1,048,576-Gaussian step, in PLY, SPZ, and SOG) into a temporary directory,
#   checks the bytes against the recorded hashes, measures every `ci: true`
#   row through scripts/benchmark_import.py one process per asset, and gates
#   each row on the manifest's expectations and resource targets. The report
#   lands in this build directory; the generated assets do not.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "benchmark corpus: Python3 not found; skipping")
    return()
endif()

set(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(_tool "${_repo}/scripts/benchmark_corpus.py")

add_test(
    NAME benchmark_corpus_manifest
    COMMAND "${Python3_EXECUTABLE}" "${_tool}" verify)

set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

set(_env
    "PATH=path_list_prepend:${_usd_root}/bin"
    "PATH=path_list_prepend:${_usd_root}/lib"
    "PYTHONPATH=path_list_prepend:${_usd_root}/lib/python"
    "PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")
foreach(_bundle gaussian-ply gaussian-spz gaussian-sog)
    list(APPEND _env
        "PATH=path_list_prepend:${_repo}/plugins/${_bundle}/lib"
        "PXR_PLUGINPATH_NAME=path_list_prepend:${_repo}/plugins/${_bundle}/plugin/resources/${_bundle}")
endforeach()

add_test(
    NAME benchmark_corpus_ci
    COMMAND "${Python3_EXECUTABLE}" "${_tool}" run --ci
        --report "${CMAKE_CURRENT_BINARY_DIR}/benchmark-report.json")
set_tests_properties(benchmark_corpus_ci PROPERTIES
    ENVIRONMENT_MODIFICATION "${_env}"
    LABELS "benchmark"
    TIMEOUT 1800)
