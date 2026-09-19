#pragma once

#include "../ecs/components.h"
#include "../util/byte_cache.h"
#include "../util/shared_byte_cache.h"
#include <bit>
#include <array>
#include "../util/code_wrap.h"

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
    size_t signatureScans_ = 0, hunkScans_ = 0, wrapScans_ = 0, wrapHits_ = 0;
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
        if (auto value = wraps_.get(key)) return {std::move(value)};
        auto value = build();
        value.shrink_to_fit();
        const auto bytes = value.capacity() * sizeof(size_t);
        if (auto owned = wraps_.put(std::move(key), value, bytes)) return {std::move(owned)};
        return {std::make_shared<const std::vector<size_t>>(std::move(value))};
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
    size_t bytes() const { return signatures_.bytes() + wraps_.bytes(); }
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
