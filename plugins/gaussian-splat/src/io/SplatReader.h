// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openstrata::gs::splat {

// One `.splat` record: position (3 x f32), linear scale (3 x f32), RGBA
// (4 x u8), and rotation (4 x u8), little-endian, with no header
// (SPLAT_FORMAT.md §2).
inline constexpr std::size_t kRecordBytes = 32;

// What the file size alone says about a `.splat` file.
struct SplatLayout {
    std::uint64_t fileBytes = 0;
    std::size_t recordCount = 0;
};

// The raw records of a `.splat` file, exactly `recordCount * kRecordBytes`
// bytes. The reader interprets none of them.
struct SplatDocument {
    SplatLayout layout;
    std::vector<unsigned char> records;
};

// Container reading for the headerless `.splat` format. Its only structure is
// the record size, so the reader checks the file size, the shared count
// ceiling, and the signatures of the formats a `.splat` file is most often
// renamed from (SPLAT_FORMAT.md §4). Record values are left to the decoder.
class SplatReader {
  public:
    // The routing gate: the file exists and its size is a non-zero multiple
    // of the record size. It needs only a stat, and a true result is a
    // routing decision, not a promise that Read() succeeds.
    bool CanRead(const std::string& path) const noexcept;

    // Checks the size, the count ceiling, and the signature without reading
    // the records. A metadata-only read uses this alone.
    bool ReadLayout(const std::string& path, SplatLayout* layout,
                    std::string* error = nullptr) const;

    // ReadLayout(), then the whole record payload.
    bool Read(const std::string& path, SplatDocument* document,
              std::string* error = nullptr) const;
};

} // namespace openstrata::gs::splat
