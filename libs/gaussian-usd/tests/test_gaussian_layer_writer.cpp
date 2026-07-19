// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/usd/GaussianLayerWriter.h"

#include "pxr/usd/sdf/layer.h"

#include <iostream>
#include <limits>
#include <string>

namespace gs = openstrata::gs;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

// Distinct sentinels per member, so each test pins a failure to the exact
// member it must come from rather than merely observing that some code was
// emitted.
//
// Three of the six members are reachable from a unit test: internalError,
// cloudValidationFailed, and extentOverflow. The remaining three
// (stageCreationFailed, scaffoldAuthoringFailed, attributeAuthoringFailed)
// fire only when OpenUSD itself fails to create a stage, define a prim, or set
// an attribute, which cannot be provoked without injecting a failing USD
// implementation. They are left uncovered deliberately: the call sites are
// assigned by name at each bundle, which is what removes the swap hazard the
// covered members are additionally tested for.
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

// The three-sigma extent bound is computed in double and rejected when it
// leaves float range. A scale of FLT_MAX is finite and strictly positive, so
// it passes shared validation and reaches the extent loop, where 3 * FLT_MAX
// does not fit back into a float.
void TestExtentOverflowReportsExtentError()
{
    const gs::usd::GaussianLayerWriter writer(kCodes);
    gs::GaussianCloudData cloud = MakeValidCloud();
    const float huge = std::numeric_limits<float>::max();
    cloud.scales[0] = {huge, huge, huge};

    PXR_NS::SdfLayerRefPtr layer;
    std::string error;
    CHECK(!writer.WriteToLayer(std::move(cloud), "test", &layer, &error));
    CHECK(StartsWith(error, "[TEST-EXTENT] "));
    CHECK(!layer);
}

// A null code would otherwise be appended to a std::string, which is undefined
// behavior. The diagnostic path must degrade to a visible placeholder instead:
// it runs only when something has already gone wrong.
void TestMissingCodeDoesNotCrash()
{
    gs::usd::LayerWriterDiagnosticCodes incomplete;
    incomplete.cloudValidationFailed = "TEST-VALIDATION";
    // internalError deliberately left null.
    const gs::usd::GaussianLayerWriter writer(incomplete);

    std::string error;
    CHECK(!writer.WriteToLayer(MakeValidCloud(), "test", nullptr, &error));
    CHECK(StartsWith(error, "["));
    CHECK(error.find("null layer output") != std::string::npos);
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
    TestExtentOverflowReportsExtentError();
    TestMissingCodeDoesNotCrash();
    TestValidCloudAuthorsLayer();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gaussianUsd layer writer tests passed\n";
    return 0;
}
