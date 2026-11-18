#pragma once

#include "../ecs/components.h"
#include "../util/byte_cache.h"
#include "../util/shared_byte_cache.h"
#include <bit>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include "../util/code_wrap.h"
#include "../util/code_gutter.h"

namespace ui {

struct MetricRows {
    std::shared_ptr<const std::vector<size_t>> values;
    bool trailing = false;
    size_t size() const { return values ? values->size() + static_cast<size_t>(trailing) : 0; }
    bool empty() const { return size() == 0; }
    size_t operator[](size_t index) const { return index < values->size() ? (*values)[index] : values->back(); }
    size_t back() const { return values->back(); }
};

class DiffMetricsCache {
    ByteCache<std::string> signatures_{2 * 1024 * 1024};
    SharedByteCache<std::vector<size_t>> wraps_{3 * 1024 * 1024};
    // Per-hunk and per-file row sums live apart from per-line wraps: a large
    // review touches more lines per frame than wraps_ holds, and sharing one
    // LRU let that churn evict the sums that let off-screen lines skip.
    SharedByteCache<std::vector<size_t>> rows_{4 * 1024 * 1024};
    size_t signatureScans_ = 0, hunkScans_ = 0, wrapScans_ = 0, wrapHits_ = 0;
    struct WidthTrack {
        float width = 0;
        int changes = 0;
        std::chrono::steady_clock::time_point changedAt{};
        std::vector<std::pair<float, MetricRows>> built;
    };
    std::unordered_map<std::string, WidthTrack> widthTracks_;
    static std::string metrics_key(float width, float fontSize, bool whitespace, char kind, std::string_view content) {
        const auto widthBytes = std::bit_cast<std::array<char, sizeof(float)>>(width);
        const auto fontBytes = std::bit_cast<std::array<char, sizeof(float)>>(fontSize);
        std::string key;
        key.reserve(widthBytes.size() + fontBytes.size() + 3 + content.size());
        key.append(widthBytes.data(), widthBytes.size());
        key.append(fontBytes.data(), fontBytes.size());
        key.push_back(whitespace ? '1' : '0');
        key.push_back(code_gutter::showNumbers ? 'n' : '-');
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
    std::string hunk_key(const ecs::FileDiff& file, const ecs::DiffHunk& hunk) {
        auto key = "h" + std::to_string(file.renderIdentity) + ":" + std::to_string(hunk.renderIdentity);
        if (const auto* value = signatures_.get(key)) return *value;
        ++hunkScans_;
        auto value = ecs::ReviewComponent::hunk_key(file.filePath, hunk);
        const auto bytes = value.capacity();
        signatures_.put(std::move(key), value, bytes);
        return value;
    }
    bool reviewed(const ecs::ReviewComponent& review, const std::string& scope, const ecs::FileDiff& file) {
        return ecs::file_reviewed(review, scope, file,
            [this](const auto& value) { return signature(value); },
            [this](const auto& value, const auto& hunk) { return hunk_key(value, hunk); });
    }
    size_t hunk_scans() const { return hunkScans_; }
    template<class Measure>
    MetricRows wraps_view(const std::string& text, float width, float fontSize, bool whitespace, Measure measure, std::string_view identity = {}) {
        // Breaks are computed on an 8 px width grid: during a resize drag the
        // available width changes every pixel and exact widths would miss
        // this cache on every step. Flooring can only narrow the text column
        // (callers already reserve padding), never overflow it, and the row
        // builders use the same function so heights stay consistent.
        width = std::floor(width / 8.f) * 8.f;
        auto key = metrics_key(width, fontSize, whitespace, identity.empty() ? 't' : 'i', identity.empty() ? std::string_view(text) : identity);
        if (auto value = wraps_.get(key)) { ++wrapHits_; return {std::move(value)}; }
        ++wrapScans_;
        auto value = code_wrap::breaks(text, width, measure);
        value.shrink_to_fit();
        auto bytes = value.capacity() * sizeof(size_t);
        if (auto owned = wraps_.put(std::move(key), value, bytes)) return {std::move(owned)};
        return {std::make_shared<const std::vector<size_t>>(std::move(value))};
    }
    template<class Measure>
    std::vector<size_t> wraps(const std::string& text, float width, float fontSize, bool whitespace, Measure measure, std::string_view identity = {}) {
        return *wraps_view(text, width, fontSize, whitespace, measure, identity).values;
    }
    template<class Build>
    MetricRows rows_view(std::string_view identity, float width, float fontSize, bool whitespace, char kind, Build build) {
        auto key = metrics_key(width, fontSize, whitespace, kind, identity);
        if (auto value = rows_.get(key)) return {std::move(value)};
        // Resize-friendly: while this row set's width keeps changing (a live
        // drag), reuse the nearest already-built width instead of re-wrapping
        // on every pixel; rebuild exactly once the width has been stable for
        // ~100 ms. Heights are briefly approximate mid-drag, exact on settle,
        // and the reading anchor is line-based so it survives the swap.
        auto& track = widthTracks_[metrics_key(0.f, fontSize, whitespace, kind, identity)];
        const auto now = std::chrono::steady_clock::now();
        if (track.width != width) {
            // A lone change (window snap, e2e resize) rebuilds exactly right
            // away; only repeated changes inside the window count as a drag.
            track.changes = now - track.changedAt < std::chrono::milliseconds(100) ? track.changes + 1 : 1;
            track.width = width;
            track.changedAt = now;
        }
        const bool resizing = track.changes >= 2 && now - track.changedAt < std::chrono::milliseconds(100);
        if (resizing && !track.built.empty()) {
            const auto best = std::min_element(track.built.begin(), track.built.end(),
                [width](const auto& a, const auto& b) { return std::abs(a.first - width) < std::abs(b.first - width); });
            return best->second;
        }
        auto value = build();
        value.shrink_to_fit();
        const auto bytes = value.capacity() * sizeof(size_t);
        auto owned = rows_.put(std::move(key), value, bytes);
        MetricRows result{owned ? std::move(owned) : std::make_shared<const std::vector<size_t>>(std::move(value))};
        track.built.emplace_back(width, result);
        if (track.built.size() > 3) track.built.erase(track.built.begin());
        if (widthTracks_.size() > 256) widthTracks_.clear();
        return result;
    }
    template<class Build>
    MetricRows source_rows_view(std::uint64_t identity, float width, float fontSize, bool whitespace, Build build) {
        const auto identityBytes = std::bit_cast<std::array<char, sizeof(identity)>>(identity);
        return rows_view({identityBytes.data(), identityBytes.size()}, width, fontSize, whitespace, 'r', build);
    }
    template<class Build>
    std::vector<size_t> source_rows(std::uint64_t identity, float width, float fontSize, bool whitespace, Build build) {
        return *source_rows_view(identity, width, fontSize, whitespace, build).values;
    }
    size_t signature_scans() const { return signatureScans_; }
    size_t wrap_scans() const { return wrapScans_; }
    size_t wrap_hits() const { return wrapHits_; }
    size_t bytes() const { return signatures_.bytes() + wraps_.bytes() + rows_.bytes(); }
};

inline DiffMetricsCache& diff_metrics() {
    static DiffMetricsCache cache;
    return cache;
}

inline bool reviewed_file(const ecs::ReviewComponent& review, const std::string& scope, const ecs::FileDiff& file) {
    return diff_metrics().reviewed(review, scope, file);
}

inline bool reviewed_file(const ecs::ReviewComponent& review, const std::string& scope, const reading::FileSummary& file) {
    return ecs::file_reviewed(review, scope, file);
}

template<class File>
inline ecs::ReviewProgress cached_review_progress(const ecs::ReviewComponent& review, const std::string& scope,
                                                 const std::vector<File>& files) {
    return ecs::review_progress(review, scope, files,
        [](const auto& value, const auto& key, const auto& file) { return reviewed_file(value, key, file); });
}

}
