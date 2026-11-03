// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/GaussianFileIO.h"
#include "openstrata/gs/testing/Utf8TestPaths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

namespace gs = openstrata::gs;

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";       \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

constexpr const char* kTag = "gaussian-core-file-io";

// The UTF-8 spelling survives the conversion unchanged, so a diagnostic built
// from the reader's path names the same file the stream opened.
void TestPathRoundTrip()
{
    const std::string name = gs::testing::kNonAsciiDirectoryName;
    CHECK(gs::testing::Utf8String(gs::PathFromUtf8(name)) == name);
    const std::string ascii = "plain/ascii/name.ply";
    CHECK(gs::testing::Utf8String(gs::PathFromUtf8(ascii)) == ascii);
}

// A file under a directory no ANSI code page can spell opens and reads back
// through OpenInputFile. On Windows this is the case a narrow-string
// std::ifstream gets wrong.
void TestOpenNonAsciiPath()
{
    const std::string directory =
        gs::testing::MakeNonAsciiFixtureDirectory(kTag, nullptr, 0);
    CHECK(!directory.empty());
    if (directory.empty()) {
        return;
    }
    const std::string path = directory + "payload.bin";
    {
        std::ofstream out(gs::PathFromUtf8(path),
                          std::ios::binary | std::ios::trunc);
        out << "gaussian";
        CHECK(static_cast<bool>(out));
    }

    std::ifstream in = gs::OpenInputFile(path);
    CHECK(static_cast<bool>(in));
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text == "gaussian");

    std::ifstream sized =
        gs::OpenInputFile(path, std::ios::binary | std::ios::ate);
    CHECK(static_cast<bool>(sized));
    CHECK(sized.tellg() == static_cast<std::streamoff>(8));

    std::ifstream missing = gs::OpenInputFile(directory + "missing.bin");
    CHECK(!missing);

    gs::testing::RemoveNonAsciiFixtureDirectory(kTag);
}

} // namespace

int main()
{
    TestPathRoundTrip();
    TestOpenNonAsciiPath();
    return failures == 0 ? 0 : 1;
}
