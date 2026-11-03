// SPDX-License-Identifier: Apache-2.0
//
// Container-level assertions for SogReader: layout detection, the two routing
// gates, ZIP walking, meta.json schema validation, lossless-WebP plane
// decoding, plane dimensions, and one case per GSSOG container diagnostic.
// Fixtures come from tools/generate_fixtures.py; nothing here decodes
// semantics or authors USD.

#include "io/SogReader.h"
#include "io/SogJson.h"
#include "io/GaussianSogDiagnostics.h"
#include "openstrata/gs/testing/Utf8TestPaths.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace gssog = openstrata::gs::sog;

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";       \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

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

// ExpectCode, plus a message fragment that tells apart failures sharing one
// code -- every container budget reports GSSOG-E016, and a test that only
// checked the code could pass on the wrong limit.
void ExpectCodeWith(const std::string& name, const char* code,
                    const std::string& fragment)
{
    gssog::SogDocument document;
    std::string error;
    if (gssog::SogReader().Read(Fixture(name), &document, &error)) {
        std::cerr << name << ": expected failure " << code << ", but the read "
                  << "succeeded\n";
        ++failures;
        return;
    }
    if (!HasCode(error, code) || error.find(fragment) == std::string::npos) {
        std::cerr << name << ": expected " << code << " mentioning \""
                  << fragment << "\", got: " << error << "\n";
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
    CHECK(
        reader.CanReadUnbundled(Fixture("unbundled-missing-plane/meta.json")));
    CHECK(!reader.CanReadUnbundled(Fixture("not-sog.json")));
    CHECK(!reader.CanReadUnbundled(Fixture("kit-one-degree0.sog")));
    CHECK(!reader.CanReadUnbundled(Fixture("no-such-file.json")));
}

// --- the bundled layout ------------------------------------------------------

void TestBundledDegree0()
{
    gssog::SogDocument document;
    std::string error;
    CHECK(gssog::SogReader().Read(Fixture("kit-one-degree0.sog"), &document,
                                  &error));
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
    CHECK(gssog::SogReader().Read(Fixture("kit-multi-degree3.sog"), &document,
                                  &error));
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
    CHECK(gssog::SogReader().Read(Fixture("kit-multi-degree3.sog"), &stored,
                                  &error));
    CHECK(gssog::SogReader().Read(Fixture("kit-multi-degree3-deflated.sog"),
                                  &deflated, &error));
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
    CHECK(gssog::SogReader().Read(Fixture("kit-multi-degree3.sog"), &bundled,
                                  &error));
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
        [&directory,
         &served](const std::string& anchorPath, const std::string& planeName,
                  std::vector<unsigned char>* bytes, std::string* error) {
            (void)anchorPath;
            (void)error;
            ++served;
            // Deliberately resolved from the directory itself rather than from
            // the anchor path, proving the loader owns resolution.
            std::ifstream in(directory / planeName, std::ios::binary);
            if (!in) {
                return false;
            }
            *bytes =
                std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                           std::istreambuf_iterator<char>());
            return true;
        });

    gssog::SogDocument document;
    std::string error;
    CHECK(reader.Read((directory / "meta.json").string(), &document, &error));
    CHECK(error.empty());
    CHECK(served == 7); // five per-Gaussian planes plus centroids and labels
    CHECK(document.metadata.gaussianCount == 3);
}

// The resolver half of the seam, as RESOLVER_MATRIX.md states it: the loader is
// handed the primary path as the anchor and a bare plane name -- never a path,
// never a URI -- it alone decides what that name resolves to, and its own
// diagnostic reaches the caller unchanged. The metadata-only read never calls
// it at all.
void TestResolverSeam()
{
    const std::filesystem::path directory =
        std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) /
        "unbundled-kit-multi-degree3";
    const std::string anchor = (directory / "meta.json").string();

    std::vector<std::string> anchors;
    std::vector<std::string> names;
    const auto recording = [&anchors, &names,
                            &directory](const std::string& anchorPath,
                                        const std::string& planeName,
                                        std::vector<unsigned char>* bytes,
                                        std::string* error) {
        (void)error;
        anchors.push_back(anchorPath);
        names.push_back(planeName);
        std::ifstream in(directory / planeName, std::ios::binary);
        *bytes = std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                            std::istreambuf_iterator<char>());
        return static_cast<bool>(in) || in.eof();
    };

    gssog::SogDocument document;
    std::string error;
    CHECK(gssog::SogReader(recording).Read(anchor, &document, &error));
    const std::vector<std::string> expected = {
        "means_l.webp", "means_u.webp",       "scales.webp",    "quats.webp",
        "sh0.webp",     "shN_centroids.webp", "shN_labels.webp"};
    CHECK(names == expected);
    for (const std::string& seen : anchors) {
        CHECK(seen == anchor);
    }

    // Metadata-only: meta.json alone, not one companion requested.
    anchors.clear();
    names.clear();
    gssog::SogMetadata metadata;
    CHECK(gssog::SogReader(recording).ReadMetadata(anchor, &metadata, &error));
    CHECK(metadata.gaussianCount == 3);
    CHECK(names.empty());

    // A name that is a path or a URI is refused before the loader sees it, so
    // no resolver is ever asked to interpret one.
    for (const char* fixture : {"unbundled-backslash-plane-name/meta.json",
                                "unbundled-uri-plane-name/meta.json"}) {
        names.clear();
        error.clear();
        CHECK(!gssog::SogReader(recording).Read(Fixture(fixture), &document,
                                                &error));
        CHECK(HasCode(error, gssog::diag::kMalformedMetadata));
        CHECK(names.empty());
        ExpectCode(fixture, gssog::diag::kMalformedMetadata);
    }

    // The loader's own diagnostic is what the caller sees.
    const gssog::SogReader refusing(
        [](const std::string&, const std::string& planeName,
           std::vector<unsigned char>*, std::string* failure) {
            *failure =
                gssog::diag::Format(gssog::diag::kMissingPlane,
                                    "resolver refused '" + planeName + "'");
            return false;
        });
    error.clear();
    CHECK(!refusing.Read(anchor, &document, &error));
    CHECK(error == gssog::diag::Format(gssog::diag::kMissingPlane,
                                       "resolver refused 'means_l.webp'"));
}

// The primary-asset seam the file-format plugin uses to read the `.sog`
// archive or `meta.json` through `ArAsset`: the loader receives an identifier
// the reader never interprets, so an in-memory "asset" whose name is not a
// filesystem path must read exactly like the file it was copied from.
// RESOLVER_MATRIX.md's package-relative and URI-style rows rest on this.
struct MemoryAsset {
    std::vector<unsigned char> bytes;
    std::vector<std::size_t> limits; // every `limit` the reader asked for

    gssog::SogPrimaryLoader Loader(const std::string& expectedName)
    {
        return [this, expectedName](const std::string& path, std::size_t limit,
                                    std::vector<unsigned char>* out,
                                    std::uint64_t* size, std::string* error) {
            limits.push_back(limit);
            if (path != expectedName) {
                *error = gssog::diag::Format(gssog::diag::kUnreadableFile,
                                             "no asset named '" + path + "'");
                return false;
            }
            *size = bytes.size();
            const std::size_t want = std::min(bytes.size(), limit);
            out->assign(bytes.begin(),
                        bytes.begin() + static_cast<std::ptrdiff_t>(want));
            return true;
        };
    }
};

std::vector<unsigned char> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

void TestInjectedPrimaryLoader()
{
    // Bundled: the whole archive is served from memory under a URI-like name.
    {
        const std::string name = "mem://assets/scene.sog";
        MemoryAsset asset;
        asset.bytes = ReadBytes(Fixture("kit-multi-degree3.sog"));
        const gssog::SogReader reader(gssog::SogCompanionLoader(),
                                      asset.Loader(name));

        CHECK(reader.CanReadBundled(name));
        CHECK(!reader.CanReadUnbundled(name));

        gssog::SogDocument fromMemory;
        gssog::SogDocument fromFile;
        std::string error;
        asset.limits.clear();
        CHECK(reader.Read(name, &fromMemory, &error));
        CHECK(error.empty());
        CHECK(gssog::SogReader().Read(Fixture("kit-multi-degree3.sog"),
                                      &fromFile, &error));
        CHECK(fromMemory.metadata.gaussianCount ==
              fromFile.metadata.gaussianCount);
        CHECK(fromMemory.shCodebook == fromFile.shCodebook);
        CHECK(fromMemory.meansLow.rgba == fromFile.meansLow.rgba);
        CHECK(fromMemory.shLabels.rgba == fromFile.shLabels.rgba);
        CHECK(fromMemory.sourceBytes == asset.bytes.size());
        // A 4-byte layout probe, then one bulk read under the archive bound.
        CHECK(asset.limits.size() == 2);
        CHECK(!asset.limits.empty() && asset.limits.front() == 4);

        gssog::SogMetadata metadata;
        CHECK(reader.ReadMetadata(name, &metadata, &error));
        CHECK(metadata.gaussianCount == fromFile.metadata.gaussianCount);
    }

    // Unbundled: meta.json from memory under a package-relative name; the
    // companions are anchored on that same name, never on a filesystem path.
    {
        const std::string name = "scene.usdz[meta.json]";
        const std::filesystem::path directory =
            std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) /
            "unbundled-kit-multi-degree3";
        MemoryAsset asset;
        asset.bytes = ReadBytes(directory / "meta.json");

        std::vector<std::string> anchors;
        const gssog::SogReader reader(
            [&anchors, &directory](
                const std::string& anchorPath, const std::string& planeName,
                std::vector<unsigned char>* bytes, std::string*) {
                anchors.push_back(anchorPath);
                *bytes = ReadBytes(directory / planeName);
                return !bytes->empty();
            },
            asset.Loader(name));

        CHECK(reader.CanReadUnbundled(name));
        CHECK(!reader.CanReadBundled(name));

        gssog::SogDocument document;
        std::string error;
        CHECK(reader.Read(name, &document, &error));
        CHECK(error.empty());
        CHECK(document.metadata.gaussianCount == 3);
        CHECK(anchors.size() == 7);
        for (const std::string& seen : anchors) {
            CHECK(seen == name);
        }
    }

    // The loader's own diagnostic reaches the caller unchanged, and a routing
    // gate that cannot read the asset declines it rather than throwing.
    {
        MemoryAsset asset;
        const gssog::SogReader reader(gssog::SogCompanionLoader(),
                                      asset.Loader("expected"));
        gssog::SogDocument document;
        std::string error;
        CHECK(!reader.Read("other", &document, &error));
        CHECK(error == gssog::diag::Format(gssog::diag::kUnreadableFile,
                                           "no asset named 'other'"));
        CHECK(!reader.CanReadBundled("other"));
        CHECK(!reader.CanReadUnbundled("other"));
    }

    // An oversized meta.json is refused from the size the probe reports: the
    // reader never asks for the bulk read.
    {
        MemoryAsset asset;
        asset.bytes.assign(4 * 1024 * 1024 + 1, ' ');
        asset.bytes.front() = '{';
        const gssog::SogReader reader(gssog::SogCompanionLoader(),
                                      asset.Loader("big.json"));
        gssog::SogDocument document;
        std::string error;
        CHECK(!reader.Read("big.json", &document, &error));
        CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
        CHECK(asset.limits.size() == 1);
    }

    // A loader that breaks its contract (more bytes than asked for) is caught
    // before any bound is trusted.
    {
        const gssog::SogReader reader(gssog::SogCompanionLoader(),
                                      [](const std::string&, std::size_t limit,
                                         std::vector<unsigned char>* out,
                                         std::uint64_t* size, std::string*) {
                                          out->assign(limit + 1, '{');
                                          *size = limit + 1;
                                          return true;
                                      });
        gssog::SogDocument document;
        std::string error;
        CHECK(!reader.Read("liar", &document, &error));
        CHECK(HasCode(error, gssog::diag::kInternalError));
    }
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
    CHECK(gssog::SogReader().ReadMetadata(Fixture("kit-one-degree0.sog"),
                                          &bundled, &error));
    CHECK(bundled.gaussianCount == 1);
    CHECK(bundled.shBands == 0);

    // A zero-Gaussian file is rejected here exactly as in a full read.
    gssog::SogMetadata rejected;
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(Fixture("empty-count.sog"),
                                           &rejected, &error));
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
    ExpectCode("too-many-gaussians/meta.json",
               gssog::diag::kImportLimitExceeded);
    ExpectCode("count-exceeds-plane.sog", gssog::diag::kInvalidGaussianCount);
    ExpectCode("bad-bands.sog", gssog::diag::kInvalidShBands);
    ExpectCode("short-codebook.sog", gssog::diag::kInvalidCodebook);

    // Integral JSON numbers beyond the range of a 64-bit integer. Every one of
    // these reaches the diagnostic that reports the offending value, so the
    // formatting must not convert it to an integer type: that conversion is
    // undefined behaviour for an out-of-range double, and a sanitizer build
    // aborts on it rather than printing a wrapped number.
    ExpectCode("huge-version.sog", gssog::diag::kUnsupportedVersion);
    ExpectCode("huge-count.sog", gssog::diag::kInvalidGaussianCount);
    ExpectCode("huge-bands.sog", gssog::diag::kInvalidShBands);
    ExpectCode("huge-palette-count.sog", gssog::diag::kMalformedMetadata);
    // A plane name that tries to leave its own directory is rejected as
    // malformed metadata rather than followed, and so is one naming a Windows
    // character device -- which carries no path separator, so only the
    // reserved-name rule refuses it.
    ExpectCode("escaping-plane-name.sog", gssog::diag::kMalformedMetadata);
    ExpectCode("device-plane-name.sog", gssog::diag::kMalformedMetadata);
    ExpectCode("missing-plane.sog", gssog::diag::kMissingPlane);
    ExpectCode("lossy-plane.sog", gssog::diag::kMalformedPlane);
    ExpectCode("corrupt-plane.sog", gssog::diag::kMalformedPlane);

    // Missing files and null outputs.
    gssog::SogDocument document;
    std::string error;
    CHECK(!gssog::SogReader().Read(Fixture("no-such-file.sog"), &document,
                                   &error));
    CHECK(HasCode(error, gssog::diag::kUnreadableFile));
    error.clear();
    CHECK(!gssog::SogReader().Read(Fixture("kit-one-degree0.sog"), nullptr,
                                   &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(Fixture("kit-one-degree0.sog"),
                                           nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));
}

// --- hostile input: the container budgets of IMPORT_LIMITS.md ---------------

// Each fixture is small on disk; what is hostile is the size it declares. The
// reader must refuse every one before sizing a buffer from that declaration
// (an ASan build of this test would otherwise trip on the allocation).
void TestContainerBudgets()
{
    ExpectCodeWith("too-many-entries.sog", gssog::diag::kImportLimitExceeded,
                   "-entry limit");
    ExpectCodeWith("oversized-entry.sog", gssog::diag::kImportLimitExceeded,
                   "per-entry extraction limit");
    ExpectCodeWith("oversized-expansion.sog", gssog::diag::kImportLimitExceeded,
                   "expanded bytes");
    ExpectCodeWith("oversized-plane-dimensions.sog",
                   gssog::diag::kImportLimitExceeded, "-pixel dimension limit");
    ExpectCodeWith("oversized-plane-bytes.sog",
                   gssog::diag::kImportLimitExceeded, "-byte decoded limit");
    ExpectCodeWith("duplicate-entry.sog", gssog::diag::kMalformedArchive,
                   "duplicate entry");
    ExpectCodeWith("duplicate-plane-name.sog", gssog::diag::kMalformedMetadata,
                   "more than once");

    // The archive budgets guard the central directory, which the
    // metadata-only path walks too: it must not report a count for an
    // archive a full read refuses.
    gssog::SogMetadata metadata;
    std::string error;
    CHECK(!gssog::SogReader().ReadMetadata(Fixture("too-many-entries.sog"),
                                           &metadata, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(Fixture("oversized-expansion.sog"),
                                           &metadata, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
}

// An unbundled meta.json one byte past the 4 MiB bound. Written at test time
// rather than committed, and refused from its size before the parser runs --
// by the full read and the metadata-only read alike.
void TestOversizedUnbundledMetadata()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-sog-reader-oversized-meta";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "meta.json";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << '{';
        const std::string spaces(64 * 1024, ' ');
        for (int i = 0; i < 64; ++i) {
            out << spaces;
        }
        out << '}';
        CHECK(static_cast<bool>(out));
    }

    gssog::SogDocument document;
    std::string error;
    CHECK(!gssog::SogReader().Read(path.string(), &document, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
    CHECK(error.find("meta.json is") != std::string::npos);

    gssog::SogMetadata metadata;
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(path.string(), &metadata, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

// Grows `path` to `size` bytes after `prefix`, without writing the rest:
// resize_file leaves a sparse hole on Linux and macOS and an unwritten
// extension on NTFS, so a test can present a multi-gigabyte real file size
// without committing, writing, or reading those bytes.
bool MakeOversizedFile(const std::filesystem::path& path,
                       const std::vector<unsigned char>& prefix,
                       std::uintmax_t size)
{
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(prefix.data()),
                  static_cast<std::streamsize>(prefix.size()));
        if (!out) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::resize_file(path, size, error);
    if (error) {
        std::cerr << path.string() << ": could not be grown to " << size
                  << " bytes: " << error.message() << "\n";
        return false;
    }
    return true;
}

// The real-size bounds (IMPORT_LIMITS.md): a bundled archive one byte past
// 1 GiB, refused from its size before the bulk read by the full and the
// metadata-only read alike.
void TestOversizedBundledArchive()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-sog-reader-oversized-archive";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "oversized.sog";
    CHECK(MakeOversizedFile(path, {'P', 'K', 0x03, 0x04},
                            1024ull * 1024ull * 1024ull + 1));

    gssog::SogDocument document;
    std::string error;
    CHECK(!gssog::SogReader().Read(path.string(), &document, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
    CHECK(error.find("-byte archive limit") != std::string::npos);

    gssog::SogMetadata metadata;
    error.clear();
    CHECK(!gssog::SogReader().ReadMetadata(path.string(), &metadata, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
    CHECK(error.find("-byte archive limit") != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

// An unbundled companion one byte past the 512 MiB plane input bound, reached
// through the default directory loader. It is refused from its size before
// its bytes are read, while the metadata-only read, which never opens a
// plane, still succeeds.
void TestOversizedUnbundledCompanion()
{
    const std::filesystem::path source =
        std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) /
        "unbundled-kit-multi-degree3";
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-sog-reader-oversized-companion";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        std::filesystem::copy_file(entry.path(),
                                   directory / entry.path().filename());
    }

    const std::filesystem::path plane = directory / "quats.webp";
    std::vector<unsigned char> prefix;
    {
        std::ifstream in(plane, std::ios::binary);
        prefix.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
    }
    CHECK(!prefix.empty());
    CHECK(MakeOversizedFile(plane, prefix, gssog::kMaxPlaneBytes + 1));

    const std::string meta = (directory / "meta.json").string();
    gssog::SogDocument document;
    std::string error;
    CHECK(!gssog::SogReader().Read(meta, &document, &error));
    CHECK(HasCode(error, gssog::diag::kImportLimitExceeded));
    CHECK(error.find("'quats.webp'") != std::string::npos);
    CHECK(error.find("-byte input limit") != std::string::npos);

    gssog::SogMetadata metadata;
    error.clear();
    CHECK(gssog::SogReader().ReadMetadata(meta, &metadata, &error));
    CHECK(metadata.gaussianCount == 3);

    std::filesystem::remove_all(directory, ignored);
}

void TestJsonTokenLimit()
{
    std::string json = "[";
    for (std::size_t i = 0; i < gssog::kJsonMaxTokens; ++i) {
        if (i != 0) {
            json += ',';
        }
        json += '0';
    }
    json += "]";

    gssog::JsonValue value;
    std::string error;
    CHECK(!gssog::ParseJson(json.data(), json.size(), &value, &error));
    CHECK(error.find("token limit") != std::string::npos);
}

void TestJsonDepthLimit()
{
    std::string accepted;
    accepted.append(gssog::kJsonMaxDepth, '[');
    accepted += '0';
    accepted.append(gssog::kJsonMaxDepth, ']');

    gssog::JsonValue value;
    std::string error;
    CHECK(gssog::ParseJson(accepted.data(), accepted.size(), &value, &error));

    std::string rejected;
    rejected.append(gssog::kJsonMaxDepth + 1, '[');
    rejected += '0';
    rejected.append(gssog::kJsonMaxDepth + 1, ']');
    error.clear();
    CHECK(!gssog::ParseJson(rejected.data(), rejected.size(), &value, &error));
    CHECK(error.find("nesting deeper") != std::string::npos);
}

} // namespace

// Both layouts under a directory no Windows ANSI code page can spell
// (RESOLVER_MATRIX.md, non-ASCII paths): the bundled archive, the unbundled
// meta.json, and the companion planes the default directory loader composes
// beside it must all open from the UTF-8 path USD hands the file format.
void TestNonAsciiPath()
{
    const char* kTag = "gaussian-sog-reader-non-ascii";
    std::vector<std::filesystem::path> fixtures = {
        std::filesystem::path(Fixture("kit-multi-degree3.sog"))};
    for (const auto& entry : std::filesystem::directory_iterator(
             std::filesystem::path(Fixture("unbundled-kit-multi-degree3")))) {
        fixtures.push_back(entry.path());
    }
    const std::string directory =
        openstrata::gs::testing::MakeNonAsciiFixtureDirectory(
            kTag, fixtures.data(), fixtures.size());
    CHECK(!directory.empty());
    if (directory.empty()) {
        return;
    }
    const std::string bundledPath = directory + "kit-multi-degree3.sog";
    const std::string unbundledPath = directory + "meta.json";

    const gssog::SogReader reader;
    CHECK(reader.CanReadBundled(bundledPath));
    CHECK(reader.CanReadUnbundled(unbundledPath));

    gssog::SogMetadata metadata;
    std::string error;
    CHECK(reader.ReadMetadata(bundledPath, &metadata, &error));
    CHECK(reader.ReadMetadata(unbundledPath, &metadata, &error));
    CHECK(error.empty());

    gssog::SogDocument bundled;
    gssog::SogDocument unbundled;
    CHECK(reader.Read(bundledPath, &bundled, &error));
    CHECK(reader.Read(unbundledPath, &unbundled, &error));
    CHECK(error.empty());
    CHECK(bundled.metadata.gaussianCount == metadata.gaussianCount);
    CHECK(unbundled.metadata.gaussianCount == metadata.gaussianCount);
    CHECK(unbundled.shCentroids.rgba == bundled.shCentroids.rgba);

    openstrata::gs::testing::RemoveNonAsciiFixtureDirectory(kTag);
}

int main()
{
    TestRouting();
    TestBundledDegree0();
    TestBundledDegree3();
    TestDeflatedArchiveMatchesStored();
    TestUnbundledMatchesBundled();
    TestInjectedCompanionLoader();
    TestResolverSeam();
    TestInjectedPrimaryLoader();
    TestMetadataOnly();
    TestMalformedContainers();
    TestContainerBudgets();
    TestOversizedUnbundledMetadata();
    TestOversizedBundledArchive();
    TestOversizedUnbundledCompanion();
    TestJsonTokenLimit();
    TestJsonDepthLimit();
    TestNonAsciiPath();

    if (failures != 0) {
        std::cerr << failures << " SOG reader check(s) failed\n";
        return 1;
    }
    std::cout << "SOG reader checks passed\n";
    return 0;
}
