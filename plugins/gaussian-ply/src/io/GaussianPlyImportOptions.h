// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include <map>
#include <string>

namespace openstrata::gs::ply {

// Optional import behavior selected through USD file-format arguments.
// Defaults are the identity transform: the imported stage matches the source.
//
//   shDegree         - cap the imported SH degree at this value (0..3). The
//                      effective degree is min(source, requested); rest
//                      coefficients above the cap are dropped. Never raises
//                      the source degree.
//   opacityThreshold - drop Gaussians whose decoded (sigmoid-mapped) opacity
//                      is below this value (0..1).
//   scaleMultiplier  - multiply decoded linear scales by this factor
//                      (finite, > 0).
struct GaussianPlyImportOptions {
    int shDegree = -1;              // -1: keep the source degree
    float opacityThreshold = -1.0f; // < 0: keep every Gaussian
    float scaleMultiplier = 1.0f;
};

// Validates and extracts the options this plugin defines from a file-format
// argument map. Unknown keys are ignored (USD and hosts may add their own);
// known keys with unparseable or out-of-range values are errors.
bool ParseImportOptions(
    const std::map<std::string, std::string>& arguments,
    GaussianPlyImportOptions* options,
    std::string* error = nullptr);

// The SH degree the imported stage will carry for a given source degree.
int EffectiveShDegree(
    const GaussianPlyImportOptions& options, int sourceDegree);

// Applies the options to a decoded cloud in place. Fails if opacityThreshold
// removes every Gaussian.
bool ApplyImportOptions(
    const GaussianPlyImportOptions& options,
    GaussianCloudData* cloud,
    std::string* error = nullptr);

} // namespace openstrata::gs::ply
