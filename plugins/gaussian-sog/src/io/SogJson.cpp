// SPDX-License-Identifier: Apache-2.0
#include "io/SogJson.h"

#include <cmath>
#include <cstdint>
#include <ios>
#include <locale>
#include <sstream>
#include <string>

namespace openstrata::gs::sog {
namespace {

class Parser {
public:
    Parser(const char* data, std::size_t size)
        : _data(data), _size(size)
    {
        // num_get under the classic locale: the decimal separator is '.'
        // regardless of the host's global locale.
        _numbers.imbue(std::locale::classic());
    }

    bool ParseDocument(JsonValue* out, std::string* error)
    {
        SkipWhitespace();
        if (!ParseValue(out, 0)) {
            Report(error);
            return false;
        }
        SkipWhitespace();
        if (_position != _size) {
            Fail("unexpected trailing content");
            Report(error);
            return false;
        }
        return true;
    }

private:
    // --- lexing -------------------------------------------------------------

    bool AtEnd() const noexcept { return _position >= _size; }
    char Peek() const noexcept { return _data[_position]; }

    void SkipWhitespace() noexcept
    {
        while (!AtEnd()) {
            const char c = Peek();
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++_position;
        }
    }

    bool Fail(const std::string& message)
    {
        if (_message.empty()) {
            _message = message + " at byte " + std::to_string(_position);
        }
        return false;
    }

    void Report(std::string* error) const
    {
        if (error) {
            *error = _message;
        }
    }

    bool Expect(char c)
    {
        if (AtEnd() || Peek() != c) {
            return Fail(std::string("expected '") + c + "'");
        }
        ++_position;
        return true;
    }

    bool Literal(const char* text, std::size_t length)
    {
        if (_size - _position < length ||
            std::string(_data + _position, length) != text) {
            return Fail("invalid literal");
        }
        _position += length;
        return true;
    }

    // --- values -------------------------------------------------------------

    bool ParseValue(JsonValue* out, std::size_t depth)
    {
        if (depth > kJsonMaxDepth) {
            return Fail("nesting deeper than " +
                std::to_string(kJsonMaxDepth) + " levels");
        }
        if (AtEnd()) {
            return Fail("unexpected end of document");
        }
        switch (Peek()) {
        case '{':
            return ParseObject(out, depth);
        case '[':
            return ParseArray(out, depth);
        case '"':
            out->type = JsonValue::Type::String;
            return ParseString(&out->text);
        case 't':
            out->type = JsonValue::Type::Boolean;
            out->boolean = true;
            return Literal("true", 4);
        case 'f':
            out->type = JsonValue::Type::Boolean;
            out->boolean = false;
            return Literal("false", 5);
        case 'n':
            out->type = JsonValue::Type::Null;
            return Literal("null", 4);
        default:
            return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue* out, std::size_t depth)
    {
        out->type = JsonValue::Type::Object;
        if (!Expect('{')) {
            return false;
        }
        SkipWhitespace();
        if (!AtEnd() && Peek() == '}') {
            ++_position;
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(&key)) {
                return false;
            }
            for (const auto& member : out->members) {
                if (member.first == key) {
                    return Fail("duplicate object key \"" + key + "\"");
                }
            }
            SkipWhitespace();
            if (!Expect(':')) {
                return false;
            }
            SkipWhitespace();
            out->members.emplace_back(std::move(key), JsonValue{});
            if (!ParseValue(&out->members.back().second, depth + 1)) {
                return false;
            }
            SkipWhitespace();
            if (AtEnd()) {
                return Fail("unterminated object");
            }
            if (Peek() == ',') {
                ++_position;
                continue;
            }
            return Expect('}');
        }
    }

    bool ParseArray(JsonValue* out, std::size_t depth)
    {
        out->type = JsonValue::Type::Array;
        if (!Expect('[')) {
            return false;
        }
        SkipWhitespace();
        if (!AtEnd() && Peek() == ']') {
            ++_position;
            return true;
        }
        while (true) {
            SkipWhitespace();
            out->items.emplace_back();
            if (!ParseValue(&out->items.back(), depth + 1)) {
                return false;
            }
            SkipWhitespace();
            if (AtEnd()) {
                return Fail("unterminated array");
            }
            if (Peek() == ',') {
                ++_position;
                continue;
            }
            return Expect(']');
        }
    }

    bool ParseHex4(std::uint32_t* value)
    {
        if (_size - _position < 4) {
            return Fail("truncated \\u escape");
        }
        *value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = _data[_position++];
            std::uint32_t digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a') + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A') + 10;
            } else {
                return Fail("invalid hex digit in \\u escape");
            }
            *value = (*value << 4) | digit;
        }
        return true;
    }

    static void AppendUtf8(std::uint32_t code, std::string* out)
    {
        if (code < 0x80) {
            out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out->push_back(static_cast<char>(0xc0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            out->push_back(static_cast<char>(0xe0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out->push_back(static_cast<char>(0xf0 | (code >> 18)));
            out->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    bool ParseString(std::string* out)
    {
        out->clear();
        if (!Expect('"')) {
            return false;
        }
        while (true) {
            if (AtEnd()) {
                return Fail("unterminated string");
            }
            const unsigned char c = static_cast<unsigned char>(_data[_position++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                return Fail("unescaped control character in string");
            }
            if (c != '\\') {
                out->push_back(static_cast<char>(c));
                continue;
            }
            if (AtEnd()) {
                return Fail("unterminated escape sequence");
            }
            const char escape = _data[_position++];
            switch (escape) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                std::uint32_t code = 0;
                if (!ParseHex4(&code)) {
                    return false;
                }
                // A high surrogate must be followed by its low surrogate; an
                // unpaired one is malformed rather than silently replaced.
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (_size - _position < 2 || _data[_position] != '\\' ||
                        _data[_position + 1] != 'u') {
                        return Fail("unpaired UTF-16 high surrogate");
                    }
                    _position += 2;
                    std::uint32_t low = 0;
                    if (!ParseHex4(&low)) {
                        return false;
                    }
                    if (low < 0xdc00 || low > 0xdfff) {
                        return Fail("invalid UTF-16 low surrogate");
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                } else if (code >= 0xdc00 && code <= 0xdfff) {
                    return Fail("unpaired UTF-16 low surrogate");
                }
                AppendUtf8(code, out);
                break;
            }
            default:
                return Fail("invalid escape sequence");
            }
        }
    }

    // RFC 8259 number grammar, validated before conversion: the JSON literals
    // NaN and Infinity do not exist, and neither do '+1', '.5', or '01'.
    bool ParseNumber(JsonValue* out)
    {
        const std::size_t start = _position;
        if (!AtEnd() && Peek() == '-') {
            ++_position;
        }
        const std::size_t integerStart = _position;
        while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
            ++_position;
        }
        const std::size_t integerDigits = _position - integerStart;
        if (integerDigits == 0) {
            return Fail("expected a value");
        }
        if (integerDigits > 1 && _data[integerStart] == '0') {
            return Fail("number has a leading zero");
        }
        if (!AtEnd() && Peek() == '.') {
            ++_position;
            const std::size_t fractionStart = _position;
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
                ++_position;
            }
            if (_position == fractionStart) {
                return Fail("number has no digits after the decimal point");
            }
        }
        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
            ++_position;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-')) {
                ++_position;
            }
            const std::size_t exponentStart = _position;
            while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
                ++_position;
            }
            if (_position == exponentStart) {
                return Fail("number has no digits in its exponent");
            }
        }

        const std::string token(_data + start, _position - start);
        _numbers.clear();
        _numbers.str(token);
        double value = 0.0;
        _numbers >> value;
        if (_numbers.fail() || !std::isfinite(value)) {
            // Reachable only through magnitudes outside double range; the
            // grammar above already rejected NaN and Infinity spellings.
            return Fail("number \"" + token + "\" is out of range");
        }
        out->type = JsonValue::Type::Number;
        out->number = value;
        return true;
    }

    const char* _data;
    std::size_t _size;
    std::size_t _position = 0;
    std::string _message;
    std::istringstream _numbers;
};

} // namespace

const JsonValue* JsonValue::Find(const std::string& key) const noexcept
{
    if (type != Type::Object) {
        return nullptr;
    }
    for (const auto& member : members) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

bool ParseJson(
    const char* data,
    std::size_t size,
    JsonValue* out,
    std::string* error)
{
    if (!out || (!data && size != 0)) {
        if (error) {
            *error = "internal: null JSON parse argument";
        }
        return false;
    }
    *out = JsonValue{};
    return Parser(data, size).ParseDocument(out, error);
}

} // namespace openstrata::gs::sog
