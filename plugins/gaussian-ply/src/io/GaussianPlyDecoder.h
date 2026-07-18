// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include <string>
#include <vector>

namespace openstrata::gs::ply {

class GaussianPlyDecoder {
public:
    bool CanRead(const std::string& path) const noexcept;

    bool Decode(
        const std::string& path,
        GaussianCloudData* cloud,
        std::vector<std::string>* warnings = nullptr,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::ply
