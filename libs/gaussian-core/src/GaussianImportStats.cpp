// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/GaussianImportStats.h"

#include <cstdio>

namespace openstrata::gs {
namespace {

// %g keeps small durations and large coordinates readable in one format and
// is locale-independent through snprintf's "C" formatting of these fields.
std::string FormatDouble(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    return buffer;
}

} // namespace

std::uint64_t ComputeDecodedByteSize(const GaussianCloudData& cloud) noexcept
{
    return static_cast<std::uint64_t>(cloud.positions.size()) * sizeof(Float3) +
        static_cast<std::uint64_t>(cloud.scales.size()) * sizeof(Float3) +
        static_cast<std::uint64_t>(cloud.rotations.size()) * sizeof(Quaternion) +
        static_cast<std::uint64_t>(cloud.opacities.size()) * sizeof(float) +
        static_cast<std::uint64_t>(cloud.dcCoefficients.size()) * sizeof(Float3) +
        static_cast<std::uint64_t>(cloud.restCoefficients.size()) * sizeof(Float3);
}

std::string FormatImportStats(const GaussianImportStats& stats)
{
    std::string line = "import";
    const auto add = [&line](const char* key, const std::string& value) {
        line += ' ';
        line += key;
        line += '=';
        line += value;
    };
    // A token that may contain spaces is quoted so the line stays splittable
    // on unquoted spaces.
    add("format", '"' + stats.sourceFormat + '"');
    if (!stats.sourceVersion.empty()) {
        add("version", '"' + stats.sourceVersion + '"');
    }
    add("gaussians", std::to_string(stats.gaussianCount));
    add("shDegree", std::to_string(stats.shDegree));
    add("sourceBytes", std::to_string(stats.sourceBytes));
    add("decodedBytes", std::to_string(stats.decodedBytes));
    if (stats.hasBounds) {
        add("boundsMin",
            FormatDouble(stats.boundsMinimum.x) + ',' +
            FormatDouble(stats.boundsMinimum.y) + ',' +
            FormatDouble(stats.boundsMinimum.z));
        add("boundsMax",
            FormatDouble(stats.boundsMaximum.x) + ',' +
            FormatDouble(stats.boundsMaximum.y) + ',' +
            FormatDouble(stats.boundsMaximum.z));
    }
    add("readSeconds", FormatDouble(stats.readSeconds));
    add("decodeSeconds", FormatDouble(stats.decodeSeconds));
    add("authorSeconds", FormatDouble(stats.authorSeconds));
    return line;
}

} // namespace openstrata::gs
