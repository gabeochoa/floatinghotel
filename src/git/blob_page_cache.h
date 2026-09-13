#pragma once

#include "content_reader.h"
#include "../util/byte_cache.h"
#include <mutex>
#include <optional>

namespace git {

inline constexpr size_t blobPageCacheBudget = 32 * 1024 * 1024;

struct BlobPage {
    std::string raw;
    ecs::FilePage page;
};

inline std::string blob_page_key(const FileRequest& request, const std::string& blob) {
    if (request.revision.empty() || blob.empty()) return {};
    return blob + "\n" + std::to_string(static_cast<int>(code_lexer::language(request.path))) + "\n" + request.page.cursor.lexical.key() + "\n" + std::to_string(static_cast<int>(request.page.action)) + ":" +
        std::to_string(request.page.cursor.offset) + ":" + std::to_string(request.page.cursor.line) + ":" +
        std::to_string(request.page.cursor.continuation) + ":" + std::to_string(request.page.targetLine) + ":" + std::to_string(request.page.leadingLines) +
        "\n" + std::to_string(request.page.cursor.column) + ":" + std::to_string(request.page.targetColumn) +
        "\n" + request.encoding + "\n" + request.detectedEncoding;
}

inline size_t blob_page_owned_bytes(const BlobPage& value) {
    return value.raw.capacity() + value.page.blob.capacity() + value.page.encoding.capacity() +
        value.page.sourceIdentity.capacity() + 4;
}

class BlobPageCache {
    mutable std::mutex mutex_;
    ByteCache<BlobPage> entries_;
    size_t hits_ = 0, misses_ = 0;
    size_t budget_;
public:
    explicit BlobPageCache(size_t budget = blobPageCacheBudget) : entries_(budget), budget_(budget) {}

    std::optional<BlobPage> get(const std::string& key) {
        if (key.empty()) return {};
        std::lock_guard lock(mutex_);
        auto* entry = entries_.get(key);
        if (entry) ++hits_;
        else ++misses_;
        return entry ? std::optional<BlobPage>(*entry) : std::nullopt;
    }

    bool put(const std::string& key, const BlobPage& value) {
        if (key.empty() || blob_page_owned_bytes(value) > budget_) return false;
        BlobPage owned = value;
        auto bytes = blob_page_owned_bytes(owned);
        std::lock_guard lock(mutex_);
        return entries_.put(key, std::move(owned), bytes);
    }

    std::pair<size_t, size_t> activity() const {
        std::lock_guard lock(mutex_);
        return {hits_, misses_};
    }

    size_t bytes() const {
        std::lock_guard lock(mutex_);
        return entries_.bytes();
    }
};

inline BlobPageCache& blob_page_cache() {
    static BlobPageCache cache;
    return cache;
}

}
