// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace openstrata::gs {

// Every path a reader receives is a UTF-8 `std::string`: OpenUSD hands file
// formats resolved paths in UTF-8 on every platform. A narrow-string
// `std::ifstream` interprets its argument in the *ANSI code page* on Windows,
// so a UTF-8 path with a character outside that code page names a file that
// does not exist. Converting through a UTF-8 `std::filesystem::path` makes the
// stream open the wide (UTF-16) name instead. POSIX paths are byte strings, so
// there the conversion is the identity.
inline std::filesystem::path PathFromUtf8(const std::string& utf8)
{
#if defined(_WIN32)
#if defined(__cpp_char8_t)
    // C++20 deprecates u8path in favour of the char8_t constructor.
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
#else
    return std::filesystem::path(utf8);
#endif
}

// The one way a reader opens a source file by path, so no bundle reintroduces
// the narrow-string open.
inline std::ifstream OpenInputFile(const std::string& utf8Path,
                                   std::ios::openmode mode = std::ios::binary)
{
    return std::ifstream(PathFromUtf8(utf8Path), mode | std::ios::in);
}

} // namespace openstrata::gs
