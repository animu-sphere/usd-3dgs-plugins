// SPDX-License-Identifier: Apache-2.0
#pragma once

// Overflow-checked size arithmetic and allocation for readers and decoders.
//
// GAUSSIAN_MODEL_CONTRACT.md §3 (*Maximum count and overflow*) requires every
// size derived from a declared Gaussian count and SH degree to be computed
// with overflow-checked arithmetic before any allocation, and an allocation
// failure to surface as a decode failure with the format's own diagnostic --
// never as an uncaught exception or a partially filled cloud. These helpers
// are that requirement's one shared implementation; a decoder that computes
// `count * perGaussian` bare is re-opening the hole they close.
//
// Container-level size validation stays format-specific (an SPZ payload bound
// is a gzip fact, a PLY vertex count is a header fact); what is shared here is
// the model-side arithmetic every format performs identically.

#include "openstrata/gs/GaussianCloudData.h"

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace openstrata::gs {

// True and writes `a * b` unless the product overflows std::size_t.
inline bool CheckedMulSize(
    std::size_t a, std::size_t b, std::size_t* product) noexcept
{
    if (!product) {
        return false;
    }
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    *product = a * b;
    return true;
}

// True and writes `a + b` unless the sum overflows std::size_t.
inline bool CheckedAddSize(
    std::size_t a, std::size_t b, std::size_t* sum) noexcept
{
    if (!sum) {
        return false;
    }
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return false;
    }
    *sum = a + b;
    return true;
}

// The model's rest-coefficient length `count * ((degree+1)^2 - 1)`
// (GAUSSIAN_MODEL_CONTRACT.md §3), overflow-checked. Fails on a degree
// outside the supported 0..kMaxShDegree range rather than computing a length
// the shared gate would reject anyway.
inline bool ComputeRestCoefficientCount(
    std::size_t gaussianCount,
    int shDegree,
    std::size_t* restCount) noexcept
{
    if (!restCount || shDegree < 0 || shDegree > kMaxShDegree) {
        return false;
    }
    const std::size_t side = static_cast<std::size_t>(shDegree) + 1;
    return CheckedMulSize(gaussianCount, side * side - 1, restCount);
}

// resize() with the failure mode the contract requires: an allocation the
// platform refuses (bad_alloc, length_error) returns false instead of
// propagating, so the decoder can report it under its own diagnostic code.
template <typename T>
inline bool TryResize(std::vector<T>* array, std::size_t count) noexcept
{
    if (!array) {
        return false;
    }
    try {
        array->resize(count);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

} // namespace openstrata::gs
