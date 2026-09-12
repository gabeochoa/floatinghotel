#pragma once

#include "../ecs/components.h"
#include "../util/byte_cache.h"
#include <bit>

namespace ui {

class DiffMetricsCache {
    ByteCache<std::string> signatures_{4 * 1024 * 1024};
    ByteCache<float> widths_{1024 * 1024};
    size_t signatureScans_ = 0, widthScans_ = 0;
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
    float width(const ecs::FileDiff& file, float fontSize, bool whitespace, bool split, Measure measure) {
        auto key = std::to_string(file.renderIdentity) + ":" + std::to_string(std::bit_cast<std::uint32_t>(fontSize)) +
            (whitespace ? ":spaces" : ":plain") + (split ? ":split" : ":inline");
        if (const auto* value = widths_.get(key)) return *value;
        ++widthScans_;
        float widest = 0.f;
        for (const auto& hunk : file.hunks)
            for (const auto& line : hunk.lines) widest = std::max(widest, measure(line));
        widths_.put(std::move(key), widest, 0);
        return widest;
    }
    size_t signature_scans() const { return signatureScans_; }
    size_t width_scans() const { return widthScans_; }
    size_t bytes() const { return signatures_.bytes() + widths_.bytes(); }
};

inline DiffMetricsCache& diff_metrics() {
    static DiffMetricsCache cache;
    return cache;
}

}
