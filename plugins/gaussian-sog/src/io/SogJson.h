// SPDX-License-Identifier: Apache-2.0
#pragma once

// A strict, dependency-free JSON reader for SOG `meta.json`.
//
// SOG_FORMAT.md §2 keeps the container reader — ZIP walking, meta.json, and
// plane indexing — in this repository and admits libraries only for ZIP
// inflation and lossless-WebP bitstream decoding. `meta.json` is part of that
// container, so it is parsed here rather than by a vendored JSON library: the
// document is a handful of small objects, numeric arrays, and file names, and
// every malformed shape has to surface as a specific GSSOG container
// diagnostic that a general-purpose parser's error strings cannot express.
//
// Deliberately strict, because a permissive parser would let a defective
// export through as a plausible asset: no comments, no trailing commas, no
// unquoted keys, no duplicate keys, no NaN/Infinity literals, and a bounded
// nesting depth. Numbers are read under the classic locale, so a host whose
// global locale uses a decimal comma cannot change how positions decode.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace openstrata::gs::sog {

class JsonValue {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;                                    // Type::String
    std::vector<JsonValue> items;                        // Type::Array
    std::vector<std::pair<std::string, JsonValue>> members; // Type::Object

    bool IsNull() const noexcept { return type == Type::Null; }
    bool IsBoolean() const noexcept { return type == Type::Boolean; }
    bool IsNumber() const noexcept { return type == Type::Number; }
    bool IsString() const noexcept { return type == Type::String; }
    bool IsArray() const noexcept { return type == Type::Array; }
    bool IsObject() const noexcept { return type == Type::Object; }

    // Object member lookup, or null when absent or not an object. Member
    // counts here are single digits, so a linear scan beats a map.
    const JsonValue* Find(const std::string& key) const noexcept;
};

// Nesting bound. SOG `meta.json` nests three levels (document, property,
// array); the limit only exists so a hostile file cannot recurse the parser
// into a stack overflow during format routing.
inline constexpr std::size_t kJsonMaxDepth = 32;

// Parses `size` bytes as one complete JSON document. Trailing whitespace is
// allowed, trailing content is not. `error` receives a bare message with no
// diagnostic code: the caller owns which GSSOG code the failure carries.
bool ParseJson(
    const char* data,
    std::size_t size,
    JsonValue* out,
    std::string* error);

} // namespace openstrata::gs::sog
