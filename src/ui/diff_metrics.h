#pragma once

#include "../ecs/components.h"
#include "../util/byte_cache.h"
#include <bit>
#include <array>
#include "../util/code_wrap.h"

namespace ui {

class DiffMetricsCache {
    ByteCache<std::string> signatures_{2 * 1024 * 1024};
    ByteCache<std::vector<size_t>> wraps_{3 * 1024 * 1024};
    size_t signatureScans_ = 0, wrapScans_ = 0, wrapHits_ = 0;
    static std::string metrics_key(float width, float fontSize, bool whitespace, char kind, std::string_view content) {
        const auto widthBytes = std::bit_cast<std::array<char, sizeof(float)>>(width);
        const auto fontBytes = std::bit_cast<std::array<char, sizeof(float)>>(fontSize);
        std::string key;
        key.reserve(widthBytes.size() + fontBytes.size() + 2 + content.size());
        key.append(widthBytes.data(), widthBytes.size());
        key.append(fontBytes.data(), fontBytes.size());
        key.push_back(whitespace ? '1' : '0');
        key.push_back(kind);
        key.append(content);
        return key;
    }
public:
    std::string signature(const ecs::FileDiff& file) {
        auto key = std::to_string(file.renderIdentity);
        if (const auto* value = signatures_.get(key)) return *value;
        ++signatureScans_;
        auto value = ecs::diff_signature(file);
        auto bytes = value.capacity();
        signatures_.put(std::move(key), value, bytes);
        return value;
    }
    template<class Measure>
    std::vector<size_t> wraps(const std::string& text, float width, float fontSize, bool whitespace, Measure measure, std::string_view identity = {}) {
        auto key = metrics_key(width, fontSize, whitespace, identity.empty() ? 't' : 'i', identity.empty() ? std::string_view(text) : identity);
        if (const auto* value = wraps_.get(key)) { ++wrapHits_; return *value; }
        ++wrapScans_;
        auto value = code_wrap::breaks(text, width, measure);
        value.shrink_to_fit();
        auto bytes = value.capacity() * sizeof(size_t);
        wraps_.put(std::move(key), value, bytes);
        return value;
    }
    template<class Build>
    std::vector<size_t> source_rows(std::uint64_t identity, float width, float fontSize, bool whitespace, Build build) {
        const auto identityBytes = std::bit_cast<std::array<char, sizeof(identity)>>(identity);
        auto key = metrics_key(width, fontSize, whitespace, 'r', {identityBytes.data(), identityBytes.size()});
        if (const auto* value = wraps_.get(key)) return *value;
        auto value = build();
        value.shrink_to_fit();
        const auto bytes = value.capacity() * sizeof(size_t);
        wraps_.put(std::move(key), value, bytes);
        return value;
    }
    size_t signature_scans() const { return signatureScans_; }
    size_t wrap_scans() const { return wrapScans_; }
    size_t wrap_hits() const { return wrapHits_; }
    size_t bytes() const { return signatures_.bytes() + wraps_.bytes(); }
};

inline DiffMetricsCache& diff_metrics() {
    static DiffMetricsCache cache;
    return cache;
}

}
