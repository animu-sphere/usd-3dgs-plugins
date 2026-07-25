// SPDX-License-Identifier: Apache-2.0
//
// Container-level assertions for SogReader: layout detection, the two routing
// gates, ZIP walking, meta.json schema validation, lossless-WebP plane
// decoding, plane dimensions, and one case per GSSOG container diagnostic.
// Fixtures come from tools/generate_fixtures.py; nothing here decodes
// semantics or authors USD.

#include "io/SogReader.h"
#include "io/GaussianSogDiagnostics.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace gssog = openstrata::gs::sog;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

std::string Fixture(const std::string& name)
{
    return (std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) / name).string();
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
}

void ExpectCode(const std::string& name, const char* code)
{
    gssog::SogDocument document;
    std::string error;
    if (gssog::SogReader().Read(Fixture(name), &document, &error)) {
        std::cerr << name << ": expected failure " << code << ", but the read "
                  << "succeeded\n";
        ++failures;
        return;
    }
    if (!HasCode(error, code)) {
        std::cerr << name << ": expected " << code << ", got: " << error
                  << "\n";
        ++failures;
    }
}

// --- routing ----------------------------------------------------------------

void TestRouting()
{
    const gssog::SogReader reader;

    // Bundled: the ZIP signature alone, so a damaged archive still reaches
    // Read() for a specific diagnostic instead of being disowned.
    CHECK(reader.CanReadBundled(Fixture("kit-one-degree0.sog")));
    CHECK(reader.CanReadBundled(Fixture("bad-zip.sog")));
    CHECK(!reader.CanReadBundled(Fixture("not-sog.sog")));
    CHECK(!reader.CanReadBundled(Fixture("no-such-file.sog")));

    // Unbundled: the strict gate that keeps the broad `.json` registration
    // from claiming unrelated JSON.
    CHECK(reader.CanReadUnbundled(
        Fixture("unbundled-kit-multi-degree3/meta.json")));
    CHECK(reader.CanReadUnbundled(
        Fixture("unbundled-missing-plane/meta.json")));
    CHECK(!reader.CanReadUnbundled(Fixture("not-sog.json")));
    CHECK(!reader.CanReadUnbundled(Fixture("kit-one-degree0.sog")));
    CHECK(!reader.CanReadUnbundled(Fixture("no-such-file.json")));
}

// --- the bundled layout ------------------------------------------------------

void TestBundledDegree0()
{
    gssog::SogDocument document;
    std::string error;
    CHECK(gssog::SogReader().Read(
        Fixture("kit-one-degree0.sog"), &document, &error));
    CHECK(error.empty());

    CHECK(document.metadata.version == 2);
    CHECK(document.metadata.gaussianCount == 1);
    CHECK(document.metadata.shBands == 0);

    // Every per-Gaussian plane decoded, one texel each, and no SH planes.
    CHECK(document.meansLow.width == 1);
    CHECK(document.meansLow.height == 1);
    CHECK(document.meansLow.rgba.size() == 4);
    CHECK(document.meansHigh.Present());
    CHECK(document.scales.Present());
    CHECK(document.quats.Present());
    CHECK(document.sh0.Present());
    CHECK(!document.shCentroids.Present());
    CHECK(!document.shLabels.Present());

    CHECK(document.scalesCodebook.size() == 256);
    CHECK(document.sh0Codebook.size() == 256);
    CHECK(document.shCodebook.empty());

    // The one texel the generator wrote: sh0 alpha is round(0.75 * 255), and
    // the quaternion tag names a dropped component.
    CHECK(document.sh0.Gaussian(0)[3] == 191);
    CHECK(document.quats.Gaussian(0)[3] >= 252);
    CHECK(document.sourceBytes > 0);
}

void TestBundledDegree3()
{
    gssog::SogDocument document;
    std::string error;
    CHECK(gssog::SogReader().Read(
        Fixture("kit-multi-degree3.sog"), &document, &error));
    CHECK(error.empty());

    CHECK(document.metadata.gaussianCount == 3);
    CHECK(document.metadata.shBands == 3);
    CHECK(document.shPaletteCount == 3);
    CHECK(document.shCodebook.size() == 256);

    // Three Gaussians pack into a 2x2 plane, so the reader must accept a
    // plane larger than the count and index it as i = x + y*width.
    CHECK(document.sh0.width == 2);
    CHECK(document.sh0.height == 2);
    CHECK(document.shLabels.width == 2);

    // Centroids are exactly one palette wide: 64 centroids of 15 coefficients.
    CHECK(document.shCentroids.width == 64 * 15);
    CHECK(document.shCentroids.height == 1);

    // Labels are the 16-bit palette index, low byte first.
    CHECK(document.shLabels.Gaussian(0)[0] == 0);
    CHECK(document.shLabels.Gaussian(1)[0] == 1);
    CHECK(document.shLabels.Gaussian(2)[0] == 2);
    CHECK(document.shLabels.Gaussian(2)[1] == 0);
}

// A DEFLATE-compressed archive must produce byte-identical planes to a stored
// one: the inflate path is miniz's, but the plane bytes are the contract.
void TestDeflatedArchiveMatchesStored()
{
    gssog::SogDocument stored;
    gssog::SogDocument deflated;
    std::string error;
    CHECK(gssog::SogReader().Read(
        Fixture("kit-multi-degree3.sog"), &stored, &error));
    CHECK(gssog::SogReader().Read(
        Fixture("kit-multi-degree3-deflated.sog"), &deflated, &error));
    CHECK(stored.metadata.gaussianCount == deflated.metadata.gaussianCount);
    CHECK(stored.meansLow.rgba == deflated.meansLow.rgba);
    CHECK(stored.meansHigh.rgba == deflated.meansHigh.rgba);
    CHECK(stored.scales.rgba == deflated.scales.rgba);
    CHECK(stored.quats.rgba == deflated.quats.rgba);
    CHECK(stored.sh0.rgba == deflated.sh0.rgba);
    CHECK(stored.shCentroids.rgba == deflated.shCentroids.rgba);
    CHECK(stored.shLabels.rgba == deflated.shLabels.rgba);
}

// --- the unbundled layout ----------------------------------------------------

// Both layouts converge on one document: the same cloud stored bundled and
// unbundled must read identically, plane bytes included.
void TestUnbundledMatchesBundled()
{
    gssog::SogDocument bundled;
    gssog::SogDocument unbundled;
    std::string error;
    CHECK(gssog::SogReader().Read(
        Fixture("kit-multi-degree3.sog"), &bundled, &error));
    CHECK(gssog::SogReader().Read(
        Fixture("unbundled-kit-multi-degree3/meta.json"), &unbundled, &error));
    CHECK(error.empty());

    CHECK(unbundled.metadata.gaussianCount == bundled.metadata.gaussianCount);
    CHECK(unbundled.metadata.shBands == bundled.metadata.shBands);
    CHECK(unbundled.shPaletteCount == bundled.shPaletteCount);
    CHECK(unbundled.scalesCodebook == bundled.scalesCodebook);
    CHECK(unbundled.sh0Codebook == bundled.sh0Codebook);
    CHECK(unbundled.shCodebook == bundled.shCodebook);
    CHECK(unbundled.meansLow.rgba == bundled.meansLow.rgba);
    CHECK(unbundled.quats.rgba == bundled.quats.rgba);
    CHECK(unbundled.shCentroids.rgba == bundled.shCentroids.rgba);
}

// The companion-loader seam the file-format plugin uses to reach planes through
// the asset resolver: a loader that serves bytes from anywhere must work
// without the reader touching the filesystem for planes.
void TestInjectedCompanionLoader()
{
    const std::filesystem::path directory =
        std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) /
        "unbundled-kit-multi-degree3";

    int served = 0;
    const gssog::SogReader reader(
        [&directory, &served](
            const std::string& anchorPath,
            const std::string& planeName,
            std::vector<unsigned char>* bytes,
            std::string* error) {
            (void)anchorPath;
            (void)error;
            ++served;
            // Deliberately resolved from the directory itself rather than from
            // the anchor path, proving the loader owns resolution.
            std::ifstream in(directory / planeName, std::ios::binary);
            if (!in) {
                return false;
            }
            *bytes = std::vector<unsigned char>(
                std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>());
            return true;
        });

    gssog::SogDocument document;
    std::string error;
    CHECK(reader.Read(
        (directory / "meta.json").string(), &document, &error));
    CHECK(error.empty());
    CHECK(served == 7); // five per-Gaussian planes plus centroids and labels
    CHECK(document.metadata.gaussianCount == 3);
}

// --- metadata-only -----------------------------------------------------------

// The metadata path parses meta.json and nothing else, so a document whose
// planes are missing still reports its count and degree -- and fails a full
// read. That asymmetry is deliberate (design policy §12.3).
void TestMetadataOnly()
{
    gssog::SogMetadata metadata;
    std::string error;
    CHECK(gssog::SogReader().ReadMetadata(
        Fixture("unbundled-missing-plane/meta.json"), &metadata, &error));
    CHECK(error.empty());
    CHECK(metadata.version == 2);
    CHECK(metadata.gaussianCount == 3);
    CHECK(metadata.shBands == 3);
    ExpectCode("unbundled-missing-plane/meta.json", gssog::diag::kMissingPlane);

    // Bundled metadata comes out of the archive without decoding a plane.
    gssog::SogMetadata bundled;
    CHECK(gssog::SogReader().ReadMetadata(
        Fixture("kit-one-degree0.sog"), &bundled, &error));
    CHECK(bundled.gaussianCount == 1);
    CHECK(bundled.shBands == 0);

    // A zero-Gaussian file is rejected here exactly as in a full read.
    gssog::SogMetadata rejected;
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(
        Fixture("empty-count.sog"), &rejected, &error));
    CHECK(HasCode(error, gssog::diag::kEmptyPointSet));
}

// --- one case per container diagnostic ---------------------------------------

void TestMalformedContainers()
{
    ExpectCode("not-sog.sog", gssog::diag::kNotSogContainer);
    ExpectCode("no-meta.sog", gssog::diag::kNotSogContainer);
    ExpectCode("bad-zip.sog", gssog::diag::kMalformedArchive);
    ExpectCode("malformed-meta.sog", gssog::diag::kMalformedMetadata);
    ExpectCode("version-1.sog", gssog::diag::kUnsupportedVersion);
    ExpectCode("version-3.sog", gssog::diag::kUnsupportedVersion);
    ExpectCode("empty-count.sog", gssog::diag::kEmptyPointSet);
    ExpectCode("count-exceeds-plane.sog", gssog::diag::kInvalidGaussianCount);
    ExpectCode("bad-bands.sog", gssog::diag::kInvalidShBands);
    ExpectCode("short-codebook.sog", gssog::diag::kInvalidCodebook);
    // A plane name that tries to leave its own directory is rejected as
    // malformed metadata rather than followed.
    ExpectCode("escaping-plane-name.sog", gssog::diag::kMalformedMetadata);
    ExpectCode("missing-plane.sog", gssog::diag::kMissingPlane);
    ExpectCode("lossy-plane.sog", gssog::diag::kMalformedPlane);
    ExpectCode("corrupt-plane.sog", gssog::diag::kMalformedPlane);

    // Missing files and null outputs.
    gssog::SogDocument document;
    std::string error;
    CHECK(!gssog::SogReader().Read(
        Fixture("no-such-file.sog"), &document, &error));
    CHECK(HasCode(error, gssog::diag::kUnreadableFile));
    error.clear();
    CHECK(!gssog::SogReader().Read(
        Fixture("kit-one-degree0.sog"), nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(
        Fixture("kit-one-degree0.sog"), nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));
}

} // namespace

int main()
{
    TestRouting();
    TestBundledDegree0();
    TestBundledDegree3();
    TestDeflatedArchiveMatchesStored();
    TestUnbundledMatchesBundled();
    TestInjectedCompanionLoader();
    TestMetadataOnly();
    TestMalformedContainers();

    if (failures != 0) {
        std::cerr << failures << " SOG reader check(s) failed\n";
        return 1;
    }
    std::cout << "SOG reader checks passed\n";
    return 0;
}
