#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace text_decode {

struct Result {
    std::string text;
    std::string encoding;
    bool binary = false;
    bool malformed = false;
};

inline void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

inline std::string label(std::string value, bool malformed) {
    if (malformed) value += value == "UTF-8" ? " (invalid sequences)" : " (malformed)";
    return value;
}

inline std::pair<std::string, bool> utf8_clean(std::string_view bytes, size_t offset) {
    std::string out;
    bool malformed = false;
    for (size_t i = offset; i < bytes.size();) {
        unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c <= 0x7f) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        size_t count = 0;
        std::uint32_t cp = 0;
        if (c >= 0xc2 && c <= 0xdf) { count = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { count = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 4; cp = c & 0x07; }
        else {
            append_utf8(out, 0xfffd);
            malformed = true;
            ++i;
            continue;
        }
        if (i + count > bytes.size()) {
            append_utf8(out, 0xfffd);
            malformed = true;
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t j = 1; j < count; ++j) {
            unsigned char next = static_cast<unsigned char>(bytes[i + j]);
            if ((next & 0xc0) != 0x80) ok = false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if ((count == 3 && ((c == 0xe0 && static_cast<unsigned char>(bytes[i + 1]) < 0xa0) ||
                            (c == 0xed && static_cast<unsigned char>(bytes[i + 1]) >= 0xa0))) ||
            (count == 4 && ((c == 0xf0 && static_cast<unsigned char>(bytes[i + 1]) < 0x90) ||
                            (c == 0xf4 && static_cast<unsigned char>(bytes[i + 1]) >= 0x90))))
            ok = false;
        if (!ok) {
            append_utf8(out, 0xfffd);
            malformed = true;
            ++i;
            continue;
        }
        append_utf8(out, cp);
        i += count;
    }
    return {out, malformed};
}

inline std::pair<std::string, bool> utf16_to_utf8(std::string_view bytes, bool littleEndian, size_t offset) {
    std::string out;
    bool malformed = false;
    for (size_t i = offset; i + 1 < bytes.size(); i += 2) {
        auto unit = littleEndian
            ? static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i]))
            : static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]));
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < bytes.size()) {
            auto next = littleEndian
                ? static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[i + 3]) << 8) | static_cast<unsigned char>(bytes[i + 2]))
                : static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[i + 2]) << 8) | static_cast<unsigned char>(bytes[i + 3]));
            if (next >= 0xdc00 && next <= 0xdfff) {
                append_utf8(out, 0x10000 + (((unit - 0xd800) << 10) | (next - 0xdc00)));
                i += 2;
                continue;
            }
        }
        if (unit >= 0xd800 && unit <= 0xdfff) {
            append_utf8(out, 0xfffd);
            malformed = true;
        } else {
            append_utf8(out, unit);
        }
    }
    if ((bytes.size() - offset) % 2 != 0) {
        append_utf8(out, 0xfffd);
        malformed = true;
    }
    return {out, malformed};
}

inline bool looks_utf16(std::string_view bytes, bool littleEndian) {
    size_t pairs = std::min<size_t>(bytes.size() / 2, 256);
    if (pairs < 4) return false;
    size_t zeros = 0;
    for (size_t i = 0; i < pairs; ++i) {
        unsigned char marker = static_cast<unsigned char>(bytes[i * 2 + (littleEndian ? 1 : 0)]);
        if (marker == 0) ++zeros;
    }
    return zeros * 4 >= pairs * 3;
}

inline Result decode(std::string_view bytes, const std::string& overrideEncoding = "auto") {
    auto utf16 = [&](bool littleEndian, size_t offset) {
        auto [text, malformed] = utf16_to_utf8(bytes, littleEndian, offset);
        bool binary = text.find('\0') != std::string::npos;
        return Result{std::move(text), label(littleEndian ? "UTF-16 LE" : "UTF-16 BE", malformed), binary, malformed};
    };
    auto utf8 = [&](size_t offset, std::string label) {
        auto [text, malformed] = utf8_clean(bytes, offset);
        Result result;
        result.text = std::move(text);
        result.encoding = text_decode::label(std::move(label), malformed);
        result.binary = result.text.find('\0') != std::string::npos;
        result.malformed = malformed;
        return result;
    };
    if (overrideEncoding == "utf8") return utf8(bytes.starts_with("\xef\xbb\xbf") ? 3 : 0, "UTF-8");
    if (overrideEncoding == "utf16le") return utf16(true, bytes.starts_with("\xff\xfe") ? 2 : 0);
    if (overrideEncoding == "utf16be") return utf16(false, bytes.starts_with("\xfe\xff") ? 2 : 0);
    if (bytes.starts_with("\xff\xfe")) return utf16(true, 2);
    if (bytes.starts_with("\xfe\xff")) return utf16(false, 2);
    if (bytes.starts_with("\xef\xbb\xbf")) return utf8(3, "UTF-8");
    if (looks_utf16(bytes, true)) return utf16(true, 0);
    if (looks_utf16(bytes, false)) return utf16(false, 0);
    return utf8(0, "UTF-8");
}

inline std::string next_override(const std::string& value) {
    if (value == "auto") return "utf8";
    if (value == "utf8") return "utf16le";
    if (value == "utf16le") return "utf16be";
    return "auto";
}

inline std::string override_label(const std::string& value, const std::string& detected) {
    if (value == "utf8") return detected.empty() ? "UTF-8" : detected;
    if (value == "utf16le") return detected.empty() ? "UTF-16 LE" : detected;
    if (value == "utf16be") return detected.empty() ? "UTF-16 BE" : detected;
    return "Auto: " + detected;
}

}
