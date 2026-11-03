// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianFileIO.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

namespace openstrata::gs::testing {

// A directory name no single Windows ANSI code page can spell: katakana (in
// CP932 only), Greek (in CP1253 only), and a Latin-1 accent. A reader that
// opens its path through a narrow-string stream fails to find a file under it
// on Windows whatever the system locale, unless that locale is UTF-8 itself.
// Written as escapes so the test source stays ASCII.
inline constexpr const char* kNonAsciiDirectoryName =
    "gs-utf8-\xE3\x83\xA6\xE3\x83\x8B-\xCE\xA9-\xC3\xA9";

// The UTF-8 spelling of `path`. `u8string()` is `std::string` in C++17 and
// `std::u8string` in C++20; copying the code units serves both.
inline std::string Utf8String(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// A fresh `<temp>/<tag>/<kNonAsciiDirectoryName>/` holding a copy of each
// fixture under its own name. Returns the UTF-8 directory path with a trailing
// separator, or an empty string when the host cannot create it.
inline std::string
MakeNonAsciiFixtureDirectory(const std::string& tag,
                             const std::filesystem::path* fixtures,
                             std::size_t fixtureCount)
{
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error) / tag;
    if (error) {
        return std::string();
    }
    std::filesystem::remove_all(root, error);
    const std::filesystem::path directory =
        root / PathFromUtf8(kNonAsciiDirectoryName);
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::string();
    }
    for (std::size_t i = 0; i < fixtureCount; ++i) {
        std::filesystem::copy_file(
            fixtures[i], directory / fixtures[i].filename(),
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return std::string();
        }
    }
    return Utf8String(directory) + "/";
}

// Removes `<temp>/<tag>/`.
inline void RemoveNonAsciiFixtureDirectory(const std::string& tag)
{
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error) / tag;
    if (!error) {
        std::filesystem::remove_all(root, error);
    }
}

} // namespace openstrata::gs::testing
