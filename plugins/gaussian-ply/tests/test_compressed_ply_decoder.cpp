// SPDX-License-Identifier: Apache-2.0
//
// SuperSplat compressed PLY dialect of the gaussian-ply bundle
// (docs/reference/COMPRESSED_PLY_MAPPING.md). The hand-coded fixtures come
// from tools/generate_fixtures.py; the expected values below are restated from
// the mapping document and the codes in that generator, not computed by the
// decoder under test.
#include "io/GaussianPlyDecoder.h"
#include "io/PlyReader.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
namespace gsply = openstrata::gs::ply;

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";       \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

constexpr double kShC0 = 0.28209479177387814;

std::string Fixture(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_PLY_FIXTURE_DIR) / name).string();
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
}

bool Near(double actual, double expected, double tolerance = 1.0e-5)
{
    return std::abs(actual - expected) <= tolerance;
}

double Unorm(std::uint32_t code, unsigned bits)
{
    return static_cast<double>(code) / static_cast<double>((1u << bits) - 1u);
}

double Lerp(double minimum, double maximum, double t)
{
    return minimum * (1.0 - t) + maximum * t;
}

double ShByte(int byte)
{
    if (byte == 0)
        return -4.0;
    if (byte == 255)
        return 4.0;
    return (byte + 0.5) / 32.0 - 4.0;
}

// The codes written by compressed_fixtures() in tools/generate_fixtures.py.
struct HandCoded {
    std::uint32_t position[3];
    std::uint32_t scale[3];
    int largest;
    std::uint32_t rotation[3];
    int rgba[4];
    int sh[9];
};

constexpr HandCoded kHandCoded[4] = {
    {{1, 2, 3},
     {0, 1023, 2047},
     0,
     {600, 400, 512},
     {10, 128, 250, 255},
     {0, 255, 128, 1, 2, 3, 100, 200, 254}},
    {{10, 20, 30},
     {1024, 512, 1},
     1,
     {300, 700, 520},
     {0, 64, 192, 128},
     {10, 20, 30, 40, 50, 60, 70, 80, 90}},
    {{100, 200, 300},
     {2047, 0, 1000},
     2,
     {511, 512, 900},
     {255, 1, 2, 0},
     {128, 127, 129, 0, 0, 0, 255, 255, 255}},
    {{2047, 1023, 0},
     {7, 777, 1777},
     3,
     {0, 1023, 450},
     {100, 150, 200, 51},
     {7, 77, 177, 17, 117, 217, 27, 127, 227}},
};
// COMPRESSED_CHUNK: log-scale and colour bounds (positions decode to codes).
constexpr double kScaleMin[3] = {-1.0, -2.0, 0.0};
constexpr double kScaleMax[3] = {1.0, 2.0, 0.5};
constexpr double kColorMin[3] = {-0.5, 0.0, 0.25};
constexpr double kColorMax[3] = {1.5, 1.0, 2.25};
// RDF -> RUB band-1 signs (ADR 0001).
constexpr double kBand1Flip[3] = {-1.0, -1.0, 1.0};

void CheckHandCoded(const gs::GaussianCloudData& cloud, const char* fixture)
{
    CHECK(cloud.gaussianCount == 4);
    CHECK(cloud.shDegree == 1);
    CHECK(cloud.restCoefficients.size() == 12);
    if (cloud.gaussianCount != 4 || cloud.restCoefficients.size() != 12) {
        std::cerr << fixture << ": unexpected shape\n";
        return;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const HandCoded& g = kHandCoded[i];

        // Position bounds equal the code range, so the RDF value is the code;
        // the model frame negates Y and Z.
        CHECK(Near(cloud.positions[i].x, g.position[0]));
        CHECK(Near(cloud.positions[i].y, -double(g.position[1])));
        CHECK(Near(cloud.positions[i].z, -double(g.position[2])));

        const unsigned bits[3] = {11, 10, 11};
        const float scales[3] = {cloud.scales[i].x, cloud.scales[i].y,
                                 cloud.scales[i].z};
        for (int a = 0; a < 3; ++a) {
            const double expected = std::exp(
                Lerp(kScaleMin[a], kScaleMax[a], Unorm(g.scale[a], bits[a])));
            CHECK(Near(scales[a], expected, 1.0e-6 * expected));
        }

        // Smallest three: stored components in ascending index order around
        // the largest, (u - 0.5) * sqrt(2), the largest reconstructed.
        double stored[3];
        double sum = 0.0;
        for (int k = 0; k < 3; ++k) {
            stored[k] = (Unorm(g.rotation[k], 10) - 0.5) * std::sqrt(2.0);
            sum += stored[k] * stored[k];
        }
        double q[4];
        for (int k = 0, s = 0; k < 4; ++k) {
            q[k] = k == g.largest ? std::sqrt(std::max(0.0, 1.0 - sum)) :
                                    stored[s++];
        }
        const double norm =
            std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        CHECK(Near(cloud.rotations[i].real, q[0] / norm));
        CHECK(Near(cloud.rotations[i].i, q[1] / norm));
        CHECK(Near(cloud.rotations[i].j, -q[2] / norm));
        CHECK(Near(cloud.rotations[i].k, -q[3] / norm));
        // Gaussian 3's stored components square-sum past 1 (about 1.007),
        // so the reconstruction clamps to exactly zero rather than going NaN.
        if (i == 3) {
            CHECK(sum > 1.0);
            CHECK(cloud.rotations[i].k == 0.0f);
        }

        const float dc[3] = {cloud.dcCoefficients[i].x,
                             cloud.dcCoefficients[i].y,
                             cloud.dcCoefficients[i].z};
        for (int c = 0; c < 3; ++c) {
            const double color =
                Lerp(kColorMin[c], kColorMax[c], Unorm(g.rgba[c], 8));
            CHECK(Near(dc[c], (color - 0.5) / kShC0));
        }
        CHECK(Near(cloud.opacities[i], g.rgba[3] / 255.0));

        // Channel-major source: rest k is (sh[k], sh[3 + k], sh[6 + k]).
        for (int k = 0; k < 3; ++k) {
            const gs::Float3& rest = cloud.restCoefficients[i * 3 + k];
            CHECK(Near(rest.x, kBand1Flip[k] * ShByte(g.sh[k])));
            CHECK(Near(rest.y, kBand1Flip[k] * ShByte(g.sh[3 + k])));
            CHECK(Near(rest.z, kBand1Flip[k] * ShByte(g.sh[6 + k])));
        }
    }
}

void CheckContract(const gs::GaussianCloudData& cloud, const char* what)
{
    for (const std::string& violation :
         gs::testing::CheckCloudContract(cloud)) {
        std::cerr << what << ": " << violation << '\n';
        ++failures;
    }
}

void TestHandCoded()
{
    gsply::GaussianPlyDecoder decoder;
    for (const char* fixture :
         {"compressed-sh1.ply", "compressed-reordered.ply"}) {
        const std::string path = Fixture(fixture);
        CHECK(decoder.CanRead(path));
        gs::GaussianCloudData cloud;
        std::vector<std::string> warnings;
        std::string error;
        gsply::GaussianPlyDialect dialect =
            gsply::GaussianPlyDialect::Graphdeco;
        CHECK(
            decoder.Decode(path, &cloud, &warnings, &error, nullptr, &dialect));
        CHECK(error.empty());
        CHECK(warnings.empty());
        CHECK(dialect == gsply::GaussianPlyDialect::Compressed);
        CheckHandCoded(cloud, fixture);
        CheckContract(cloud, fixture);
    }
}

// Vertex i uses chunk floor(i / 256), including the partial last chunk, and
// colour bounds outside [0, 1] are honoured.
void TestChunkIndexing()
{
    gsply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Fixture("compressed-two-chunks.ply"), &cloud, nullptr,
                         &error));
    CHECK(cloud.gaussianCount == 300);
    CHECK(cloud.shDegree == 0);
    CHECK(cloud.restCoefficients.empty());
    if (cloud.gaussianCount != 300) {
        return;
    }
    const auto checkPosition = [&](std::size_t row, double value) {
        CHECK(Near(cloud.positions[row].x, value));
        CHECK(Near(cloud.positions[row].y, -value));
        CHECK(Near(cloud.positions[row].z, -value));
    };
    checkPosition(0, 1.0);    // chunk 0 maximum
    checkPosition(255, -1.0); // chunk 0 minimum, last row of the chunk
    checkPosition(256, 12.0); // chunk 1 maximum, first row of the chunk
    checkPosition(299, 10.0); // chunk 1 minimum, last row of the file
    CHECK(Near(cloud.dcCoefficients[0].x, (2.61 - 0.5) / kShC0, 1.0e-4));
    CHECK(Near(cloud.dcCoefficients[1].x, (-0.49 - 0.5) / kShC0, 1.0e-4));
    CHECK(Near(cloud.dcCoefficients[1].y, (-0.2 - 0.5) / kShC0, 1.0e-4));
    CHECK(Near(cloud.opacities[299], 200.0 / 255.0));
    CheckContract(cloud, "compressed-two-chunks.ply");
}

void TestRenderModeWarning()
{
    gsply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(decoder.Decode(Fixture("compressed-render-mode-mip.ply"), &cloud,
                         &warnings, &error));
    CHECK(cloud.gaussianCount == 1);
    CHECK(warnings.size() == 1);
    if (warnings.size() == 1) {
        CHECK(HasCode(warnings[0], "GSPLY-W004"));
        CHECK(warnings[0].find("'mip'") != std::string::npos);
    }
}

void TestMetadata()
{
    gsply::GaussianPlyDecoder decoder;
    gsply::GaussianPlyMetadata metadata;
    std::string error;
    CHECK(decoder.DecodeMetadata(Fixture("compressed-sh1.ply"), &metadata,
                                 &error));
    CHECK(metadata.gaussianCount == 4);
    CHECK(metadata.shDegree == 1);
    CHECK(metadata.dialect == gsply::GaussianPlyDialect::Compressed);
    CHECK(gsply::SourceFormatToken(metadata.dialect) ==
          std::string("Gaussian Splatting compressed PLY"));

    CHECK(decoder.DecodeMetadata(Fixture("compressed-two-chunks.ply"),
                                 &metadata, &error));
    CHECK(metadata.gaussianCount == 300);
    CHECK(metadata.shDegree == 0);

    // The Graphdeco dialect keeps its own token.
    CHECK(
        decoder.DecodeMetadata(Fixture("degree-1-sh.ply"), &metadata, &error));
    CHECK(metadata.dialect == gsply::GaussianPlyDialect::Graphdeco);
    CHECK(gsply::SourceFormatToken(metadata.dialect) ==
          std::string("Gaussian Splatting PLY"));
}

void TestFailures()
{
    struct Case {
        const char* fixture;
        const char* code;
        // Whether the header alone decides the failure, so the metadata-only
        // path reports the same code; body defects pass metadata.
        bool headerOnly;
        // Whether CanRead claims the file (it carries the signature).
        bool claimed;
    };
    const Case cases[] = {
        {"compressed-ascii.ply", "GSPLY-E020", true, true},
        {"compressed-big-endian.ply", "GSPLY-E020", true, true},
        {"compressed-colourless-chunk.ply", "GSPLY-E021", true, true},
        {"compressed-float-packed.ply", "GSPLY-E022", true, true},
        {"compressed-extra-vertex-property.ply", "GSPLY-E022", true, true},
        {"compressed-extra-element.ply", "GSPLY-E022", true, true},
        {"compressed-sh-bad-name.ply", "GSPLY-E022", true, true},
        {"compressed-chunk-count.ply", "GSPLY-E023", true, true},
        {"compressed-sh-rows.ply", "GSPLY-E023", true, true},
        {"compressed-sh-not-rgb.ply", "GSPLY-E008", true, true},
        {"compressed-sh-degree-4.ply", "GSPLY-E017", true, true},
        {"compressed-empty.ply", "GSPLY-E002", true, true},
        {"compressed-missing-chunk.ply", "GSPLY-E001", true, false},
        {"compressed-nan-bound.ply", "GSPLY-E024", false, true},
        {"compressed-inverted-bound.ply", "GSPLY-E024", false, true},
        {"compressed-scale-overflow.ply", "GSPLY-E012", false, true},
        {"compressed-truncated.ply", "GSPLY-E015", false, true},
    };
    gsply::GaussianPlyDecoder decoder;
    for (const Case& c : cases) {
        const std::string path = Fixture(c.fixture);
        if (decoder.CanRead(path) != c.claimed) {
            std::cerr << c.fixture << ": CanRead " << !c.claimed << '\n';
            ++failures;
        }

        gs::GaussianCloudData cloud;
        std::string error;
        if (decoder.Decode(path, &cloud, nullptr, &error) ||
            !HasCode(error, c.code)) {
            std::cerr << c.fixture << ": expected " << c.code << ", got '"
                      << error << "'\n";
            ++failures;
        }

        gsply::GaussianPlyMetadata metadata;
        error.clear();
        const bool metadataOk = decoder.DecodeMetadata(path, &metadata, &error);
        if (c.headerOnly ? (metadataOk || !HasCode(error, c.code)) :
                           !metadataOk) {
            std::cerr << c.fixture << ": metadata path returned " << metadataOk
                      << " '" << error << "'\n";
            ++failures;
        }
    }
}

void TestImportStats()
{
    gsply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    gs::GaussianImportStats stats;
    CHECK(decoder.Decode(Fixture("compressed-sh1.ply"), &cloud, &warnings,
                         &error, &stats));
    CHECK(stats.sourceFormat == gsply::kCompressedSourceFormatToken);
    CHECK(stats.sourceVersion == "binary_little_endian");
    CHECK(stats.coordinateConversion ==
          gs::GaussianCoordinateConversion::RdfToRub);
    CHECK(stats.gaussianCount == 4);
    CHECK(stats.shDegree == 1);
    CHECK(stats.sourceBytes ==
          std::filesystem::file_size(Fixture("compressed-sh1.ply")));
    CHECK(stats.decodedBytes == gs::ComputeDecodedByteSize(cloud));
    CHECK(stats.readSeconds >= 0.0 && stats.decodeSeconds >= 0.0);
}

// ---------------------------------------------------------------------------
// Real-asset check: SplatTransform 3.11.2's conversion of each committed CC0
// corpus PLY decodes to the lossless source within the quantization bounds of
// COMPRESSED_PLY_MAPPING.md, evaluated per row from the file's own chunk
// table. The writer reorders rows (Morton order), so rows are matched first;
// the match must be a bijection.
// ---------------------------------------------------------------------------

struct ChunkBounds {
    float values[18];
};

std::vector<ChunkBounds> ReadChunkTable(const std::string& path)
{
    static const char* kNames[18] = {
        "min_x",       "min_y",       "min_z",       "max_x",
        "max_y",       "max_z",       "min_scale_x", "min_scale_y",
        "min_scale_z", "max_scale_x", "max_scale_y", "max_scale_z",
        "min_r",       "min_g",       "min_b",       "max_r",
        "max_g",       "max_b",
    };
    std::vector<gsply::PlyElementRows> rows(1);
    rows[0].element = "chunk";
    rows[0].properties.assign(std::begin(kNames), std::end(kNames));
    gsply::PlyHeader header;
    std::string error;
    std::vector<ChunkBounds> table;
    if (!gsply::PlyReader().ReadElementRows(path, &rows, &header, &error)) {
        std::cerr << path << ": " << error << '\n';
        ++failures;
        return table;
    }
    table.resize(rows[0].rowCount);
    for (std::size_t c = 0; c < table.size(); ++c) {
        std::memcpy(table[c].values,
                    rows[0].bytes.data() + c * rows[0].rowStride,
                    sizeof(table[c].values));
    }
    return table;
}

struct RowTolerance {
    double position[3];
    double logScale[3];
    double dc[3];
};

RowTolerance ToleranceFor(const ChunkBounds& b)
{
    // Half a code step of the 11/10/11 and 8-bit unorm grids, plus float32
    // rounding of the decoded value.
    const double steps[3] = {4094.0, 2046.0, 4094.0};
    RowTolerance t{};
    for (int a = 0; a < 3; ++a) {
        t.position[a] =
            (b.values[3 + a] - b.values[a]) / steps[a] +
            2.0e-7 * (std::abs(b.values[a]) + std::abs(b.values[3 + a]) + 1.0);
        t.logScale[a] = (b.values[9 + a] - b.values[6 + a]) / steps[a] + 1.0e-6;
        t.dc[a] =
            (b.values[15 + a] - b.values[12 + a]) / (510.0 * kShC0) + 1.0e-5;
    }
    return t;
}

double LogClamped(float scale)
{
    return std::min(20.0, std::max(-20.0, std::log(double(scale))));
}

// The largest per-attribute error as a fraction of its bound; <= 1 passes.
double MatchCost(const gs::GaussianCloudData& source, std::size_t s,
                 const gs::GaussianCloudData& decoded, std::size_t d,
                 const RowTolerance& t)
{
    const float sp[3] = {source.positions[s].x, source.positions[s].y,
                         source.positions[s].z};
    const float dp[3] = {decoded.positions[d].x, decoded.positions[d].y,
                         decoded.positions[d].z};
    double cost = 0.0;
    for (int a = 0; a < 3; ++a) {
        cost = std::max(cost, std::abs(sp[a] - dp[a]) / t.position[a]);
    }
    if (cost > 1.0) {
        return cost;
    }
    const float ss[3] = {source.scales[s].x, source.scales[s].y,
                         source.scales[s].z};
    const float ds[3] = {decoded.scales[d].x, decoded.scales[d].y,
                         decoded.scales[d].z};
    const float sd[3] = {source.dcCoefficients[s].x, source.dcCoefficients[s].y,
                         source.dcCoefficients[s].z};
    const float dd[3] = {decoded.dcCoefficients[d].x,
                         decoded.dcCoefficients[d].y,
                         decoded.dcCoefficients[d].z};
    for (int a = 0; a < 3; ++a) {
        cost = std::max(cost,
                        std::abs(LogClamped(ss[a]) - std::log(double(ds[a]))) /
                            t.logScale[a]);
        cost = std::max(cost, std::abs(sd[a] - dd[a]) / t.dc[a]);
    }
    cost = std::max(cost, std::abs(source.opacities[s] - decoded.opacities[d]) /
                              (1.0 / 510.0 + 1.0e-6));
    return cost;
}

void TestCorpusConversion(const char* asset)
{
    const std::filesystem::path dir =
        std::filesystem::path(GAUSSIAN_PLY_CORPUS_DIR) / asset;
    const std::string sourcePath =
        (dir / (std::string(asset) + "-top8192.ply")).string();
    const std::string compressedPath =
        (dir / (std::string(asset) + "-top8192.compressed.ply")).string();

    gsply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData source;
    gs::GaussianCloudData decoded;
    std::string error;
    CHECK(decoder.Decode(sourcePath, &source, nullptr, &error));
    CHECK(decoder.CanRead(compressedPath));
    CHECK(decoder.Decode(compressedPath, &decoded, nullptr, &error));
    CheckContract(decoded, asset);
    CHECK(source.gaussianCount == decoded.gaussianCount);
    CHECK(source.shDegree == decoded.shDegree);
    const std::vector<ChunkBounds> chunks = ReadChunkTable(compressedPath);
    const std::size_t count = decoded.gaussianCount;
    if (source.gaussianCount != count || chunks.size() != (count + 255) / 256) {
        std::cerr << asset << ": shape mismatch\n";
        ++failures;
        return;
    }

    const std::size_t perChannel = decoded.restCoefficients.size() / count;
    std::vector<char> used(count, 0);
    std::size_t unmatched = 0;
    double worstRotation = 0.0;
    double worstSh = 0.0;
    for (std::size_t d = 0; d < count; ++d) {
        const RowTolerance t = ToleranceFor(chunks[d / 256]);
        std::size_t best = count;
        double bestCost = std::numeric_limits<double>::infinity();
        for (std::size_t s = 0; s < count; ++s) {
            const double cost = MatchCost(source, s, decoded, d, t);
            if (cost < bestCost) {
                bestCost = cost;
                best = s;
            }
        }
        if (bestCost > 1.0 || used[best]) {
            ++unmatched;
            continue;
        }
        used[best] = 1;

        // Rotation: the EQUIVALENCE.md envelope, per component after
        // aligning the double cover.
        const gs::Quaternion& q = source.rotations[best];
        gs::Quaternion r = decoded.rotations[d];
        if (q.real * r.real + q.i * r.i + q.j * r.j + q.k * r.k < 0.0f) {
            r = {-r.real, -r.i, -r.j, -r.k};
        }
        worstRotation =
            std::max({worstRotation, double(std::abs(q.real - r.real)),
                      double(std::abs(q.i - r.i)), double(std::abs(q.j - r.j)),
                      double(std::abs(q.k - r.k))});

        // SH rest: half a 1/32 step inside the range; the two end codes are
        // pinned to +/-4 and cover up to a full step; values beyond +/-4
        // saturate. Errors are taken against the source clamped to [-4, 4].
        for (std::size_t k = 0; k < perChannel; ++k) {
            const gs::Float3& sv =
                source.restCoefficients[best * perChannel + k];
            const gs::Float3& dv = decoded.restCoefficients[d * perChannel + k];
            for (const auto& pair :
                 {std::make_pair(sv.x, dv.x), std::make_pair(sv.y, dv.y),
                  std::make_pair(sv.z, dv.z)}) {
                const double clamped =
                    std::min(4.0, std::max(-4.0, double(pair.first)));
                const double bound =
                    std::abs(pair.second) == 4.0f ? 1.0 / 32.0 : 1.0 / 64.0;
                worstSh =
                    std::max(worstSh, std::abs(clamped - pair.second) / bound);
            }
        }
    }
    if (unmatched != 0) {
        std::cerr << asset << ": " << unmatched
                  << " rows have no source row within the bounds\n";
        ++failures;
    }
    if (!(worstRotation <= 8.0e-3)) {
        std::cerr << asset << ": rotation component error " << worstRotation
                  << " > 8e-3\n";
        ++failures;
    }
    if (!(worstSh <= 1.0 + 1.0e-4)) {
        std::cerr << asset << ": SH error " << worstSh << " of its bound\n";
        ++failures;
    }
}

} // namespace

int main()
{
    TestHandCoded();
    TestChunkIndexing();
    TestRenderModeWarning();
    TestMetadata();
    TestFailures();
    TestImportStats();
    TestCorpusConversion("yashica-t4");
    TestCorpusConversion("leica-sofort");
    if (failures != 0) {
        std::cerr << failures << " compressed PLY check(s) failed\n";
        return 1;
    }
    std::cout << "all compressed PLY checks passed\n";
    return 0;
}
