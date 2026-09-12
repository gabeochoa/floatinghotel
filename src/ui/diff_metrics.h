#pragma once

#include "../ecs/components.h"
#include "../util/byte_cache.h"
#include <bit>
#include "../util/code_wrap.h"

namespace ui {

class DiffMetricsCache {
    ByteCache<std::string> signatures_{2 * 1024 * 1024};
    ByteCache<std::vector<size_t>> wraps_{3 * 1024 * 1024};
    size_t signatureScans_ = 0, wrapScans_ = 0, wrapHits_ = 0;
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
    std::vector<size_t> wraps(const std::string& text, float width, float fontSize, bool whitespace, Measure measure) {
        auto key = std::to_string(std::bit_cast<std::uint32_t>(width)) + ":" +
            std::to_string(std::bit_cast<std::uint32_t>(fontSize)) + (whitespace ? ":spaces:" : ":plain:") + text;
        if (const auto* value = wraps_.get(key)) { ++wrapHits_; return *value; }
        ++wrapScans_;
        auto value = code_wrap::breaks(text, width, measure);
        auto bytes = value.capacity() * sizeof(size_t);
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
