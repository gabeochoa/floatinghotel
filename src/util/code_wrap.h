#pragma once

#include <algorithm>
#include <string_view>
#include <vector>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace code_wrap {

inline size_t next_codepoint(std::string_view text, size_t at) {
    size_t end = at + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
    return end;
}

inline std::vector<size_t> character_ends(std::string_view text) {
    std::vector<size_t> out;
#ifdef __APPLE__
    auto string = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    if (string) {
        size_t at = 0;
        CFIndex position = 0;
        while (at < text.size()) {
            auto range = CFStringGetRangeOfComposedCharactersAtIndex(string, position);
            auto end = range.location + range.length;
            while (position < end) {
                position += static_cast<unsigned char>(text[at]) >= 0xf0 ? 2 : 1;
                at = next_codepoint(text, at);
            }
            out.push_back(at);
        }
        CFRelease(string);
        return out;
    }
#endif
    for (size_t at = 0; at < text.size();) {
        at = next_codepoint(text, at);
        out.push_back(at);
    }
    return out;
}

template<class Measure>
std::vector<size_t> breaks(std::string_view text, float width, Measure measure) {
    std::vector<size_t> out{0};
    float used = 0.f;
    size_t at = 0;
    for (size_t end : character_ends(text)) {
        float advance = measure(text.substr(at, end - at));
        if (at > out.back() && used + advance > width) {
            out.push_back(at);
            used = 0.f;
        }
        used += advance;
        at = end;
    }
    out.push_back(text.size());
    return out;
}

inline std::pair<size_t, size_t> intersect(std::pair<size_t, size_t> range, size_t begin, size_t end) {
    auto first = std::clamp(range.first, begin, end);
    auto last = std::clamp(range.second, first, end);
    return {first - begin, last - begin};
}

}
