// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/usd/GaussianLayerWriter.h"

#include "pxr/usd/sdf/layer.h"

#include <iostream>
#include <string>

namespace gs = openstrata::gs;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

// Distinct sentinels per member. LayerWriterDiagnosticCodes is six pointers of
// the same type initialized positionally by each bundle, so a swapped pair
// would compile silently and emit the wrong stable code to users. These tests
// pin each failure to the member it must come from.
constexpr gs::usd::LayerWriterDiagnosticCodes kCodes{
    "TEST-INTERNAL",
    "TEST-VALIDATION",
    "TEST-STAGE",
    "TEST-SCAFFOLD",
    "TEST-ATTRIBUTE",
    "TEST-EXTENT",
};

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

gs::GaussianCloudData MakeValidCloud()
{
    gs::GaussianCloudData cloud;
    cloud.gaussianCount = 1;
    cloud.shDegree = 0;
    cloud.positions.push_back({1.0f, 2.0f, 3.0f});
    cloud.scales.push_back({1.0f, 1.0f, 1.0f});
    cloud.rotations.push_back({});
    cloud.opacities.push_back(0.5f);
    cloud.dcCoefficients.push_back({0.1f, 0.2f, 0.3f});
    return cloud;
}

// A null output layer is internal pipeline misuse, not file content.
void TestNullLayerReportsInternalError()
{
    const gs::usd::GaussianLayerWriter writer(kCodes);
    std::string error;
    CHECK(!writer.WriteToLayer(MakeValidCloud(), "test", nullptr, &error));
    CHECK(StartsWith(error, "[TEST-INTERNAL] "));

    error.clear();
    CHECK(!writer.WriteMetadataToLayer(1, 0, "test", nullptr, &error));
    CHECK(StartsWith(error, "[TEST-INTERNAL] "));
}

// Shared cloud validation runs before any authoring, so a short array must
// surface as the validation code rather than an authoring code.
void TestInvalidCloudReportsValidationError()
{
    const gs::usd::GaussianLayerWriter writer(kCodes);
    gs::GaussianCloudData cloud = MakeValidCloud();
    cloud.positions.clear();

    PXR_NS::SdfLayerRefPtr layer;
    std::string error;
    CHECK(!writer.WriteToLayer(std::move(cloud), "test", &layer, &error));
    CHECK(StartsWith(error, "[TEST-VALIDATION] "));
    CHECK(!layer);
}

// The success paths must author a layer and leave the error untouched.
void TestValidCloudAuthorsLayer()
{
    const gs::usd::GaussianLayerWriter writer(kCodes);

    PXR_NS::SdfLayerRefPtr layer;
    std::string error;
    CHECK(writer.WriteToLayer(MakeValidCloud(), "test", &layer, &error));
    CHECK(layer);
    CHECK(error.empty());

    PXR_NS::SdfLayerRefPtr metadataLayer;
    CHECK(writer.WriteMetadataToLayer(1, 0, "test", &metadataLayer, &error));
    CHECK(metadataLayer);
    CHECK(error.empty());
}

} // namespace

int main()
{
    TestNullLayerReportsInternalError();
    TestInvalidCloudReportsValidationError();
    TestValidCloudAuthorsLayer();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gaussianUsd layer writer tests passed\n";
    return 0;
}
