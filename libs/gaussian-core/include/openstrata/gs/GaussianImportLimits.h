// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace openstrata::gs {

// Conservative shared ceiling for declared Gaussian counts. It preserves the
// committed multi-million corpus while preventing a container header from
// driving unbounded parser and model allocations.
inline constexpr std::size_t kMaxGaussianCount = 8'000'000;

inline constexpr bool IsGaussianCountWithinLimit(std::size_t count) noexcept
{
    return count <= kMaxGaussianCount;
}

} // namespace openstrata::gs