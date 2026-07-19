// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianPlyImportOptions.h"

#include "io/GaussianPlyDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace openstrata::gs::ply {
namespace {

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

bool ParseFullFloat(const std::string& text, float* value)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size()) {
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseFullInt(const std::string& text, int* value)
{
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size()) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

std::string BadArgument(
    const char* name, const std::string& value, const char* expectation)
{
    return std::string("Gaussian PLY file-format argument '") + name +
        "' = '" + value + "' is invalid; expected " + expectation + ".";
}

} // namespace

bool ParseImportOptions(
    const std::map<std::string, std::string>& arguments,
    GaussianPlyImportOptions* options,
    std::string* error)
{
    if (!options) {
        SetError(error, diag::kInternalError,
            "Import-option parsing received a null options output.");
        return false;
    }
    *options = GaussianPlyImportOptions();

    const auto shDegree = arguments.find("shDegree");
    if (shDegree != arguments.end()) {
        int parsed = 0;
        if (!ParseFullInt(shDegree->second, &parsed) ||
            parsed < 0 || parsed > 3) {
            SetError(error, diag::kInvalidFormatArgument,
                BadArgument("shDegree", shDegree->second,
                    "an integer in [0, 3]"));
            return false;
        }
        options->shDegree = parsed;
    }

    const auto opacityThreshold = arguments.find("opacityThreshold");
    if (opacityThreshold != arguments.end()) {
        float parsed = 0.0f;
        if (!ParseFullFloat(opacityThreshold->second, &parsed) ||
            !std::isfinite(parsed) || parsed < 0.0f || parsed > 1.0f) {
            SetError(error, diag::kInvalidFormatArgument,
                BadArgument("opacityThreshold", opacityThreshold->second,
                    "a number in [0, 1]"));
            return false;
        }
        options->opacityThreshold = parsed;
    }

    const auto scaleMultiplier = arguments.find("scaleMultiplier");
    if (scaleMultiplier != arguments.end()) {
        float parsed = 0.0f;
        if (!ParseFullFloat(scaleMultiplier->second, &parsed) ||
            !std::isfinite(parsed) || parsed <= 0.0f) {
            SetError(error, diag::kInvalidFormatArgument,
                BadArgument("scaleMultiplier", scaleMultiplier->second,
                    "a finite number greater than 0"));
            return false;
        }
        options->scaleMultiplier = parsed;
    }

    return true;
}

int EffectiveShDegree(
    const GaussianPlyImportOptions& options, int sourceDegree)
{
    if (options.shDegree < 0) {
        return sourceDegree;
    }
    return std::min(options.shDegree, sourceDegree);
}

bool ApplyImportOptions(
    const GaussianPlyImportOptions& options,
    GaussianCloudData* cloud,
    std::string* error)
{
    if (!cloud) {
        SetError(error, diag::kInternalError,
            "Import-option application received a null cloud.");
        return false;
    }

    if (options.scaleMultiplier != 1.0f) {
        for (Float3& scale : cloud->scales) {
            scale.x *= options.scaleMultiplier;
            scale.y *= options.scaleMultiplier;
            scale.z *= options.scaleMultiplier;
        }
    }

    if (options.opacityThreshold >= 0.0f) {
        const std::size_t restPerGaussian =
            cloud->CoefficientsPerGaussian() - 1;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < cloud->gaussianCount; ++i) {
            if (cloud->opacities[i] < options.opacityThreshold) {
                continue;
            }
            if (kept != i) {
                cloud->positions[kept] = cloud->positions[i];
                cloud->scales[kept] = cloud->scales[i];
                cloud->rotations[kept] = cloud->rotations[i];
                cloud->opacities[kept] = cloud->opacities[i];
                cloud->dcCoefficients[kept] = cloud->dcCoefficients[i];
                for (std::size_t c = 0; c < restPerGaussian; ++c) {
                    cloud->restCoefficients[kept * restPerGaussian + c] =
                        cloud->restCoefficients[i * restPerGaussian + c];
                }
            }
            ++kept;
        }
        if (kept == 0) {
            SetError(error, diag::kAllGaussiansFiltered,
                "opacityThreshold " +
                std::to_string(options.opacityThreshold) +
                " removed every Gaussian in the file.");
            return false;
        }
        cloud->gaussianCount = kept;
        cloud->positions.resize(kept);
        cloud->scales.resize(kept);
        cloud->rotations.resize(kept);
        cloud->opacities.resize(kept);
        cloud->dcCoefficients.resize(kept);
        cloud->restCoefficients.resize(kept * restPerGaussian);
        cloud->positions.shrink_to_fit();
        cloud->scales.shrink_to_fit();
        cloud->rotations.shrink_to_fit();
        cloud->opacities.shrink_to_fit();
        cloud->dcCoefficients.shrink_to_fit();
        cloud->restCoefficients.shrink_to_fit();
    }

    const int effectiveDegree = EffectiveShDegree(options, cloud->shDegree);
    if (effectiveDegree != cloud->shDegree) {
        const std::size_t oldRest = cloud->CoefficientsPerGaussian() - 1;
        const std::size_t newRest = static_cast<std::size_t>(
            (effectiveDegree + 1) * (effectiveDegree + 1) - 1);
        for (std::size_t i = 0; i < cloud->gaussianCount; ++i) {
            for (std::size_t c = 0; c < newRest; ++c) {
                cloud->restCoefficients[i * newRest + c] =
                    cloud->restCoefficients[i * oldRest + c];
            }
        }
        cloud->restCoefficients.resize(cloud->gaussianCount * newRest);
        cloud->restCoefficients.shrink_to_fit();
        cloud->shDegree = effectiveDegree;
    }

    return true;
}

} // namespace openstrata::gs::ply
