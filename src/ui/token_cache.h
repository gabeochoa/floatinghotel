#pragma once

#include "code_highlight.h"
#include "../util/byte_cache.h"
#include <memory>

namespace code_highlight {

class TokenCache {
    using Tokens = std::shared_ptr<const std::vector<Token>>;
    ByteCache<Tokens> cache_;
    size_t hits_ = 0, misses_ = 0;
    template<class Build>
    Tokens cached(std::string key, Build build) {
        if (auto* found = cache_.get(key)) { ++hits_; return *found; }
        ++misses_;
        auto tokens = std::make_shared<const std::vector<Token>>(build());
        size_t bytes = sizeof(*tokens) + tokens->capacity() * sizeof(Token) + 4 * sizeof(void*);
        for (const auto& token : *tokens) bytes += token.text.capacity();
        cache_.put(std::move(key), tokens, bytes);
        return tokens;
    }
public:
    explicit TokenCache(size_t bytes = 4 * 1024 * 1024) : cache_(bytes) {}
    Tokens get(const std::string& text, const std::string& path) {
        auto dot = path.find_last_of('.');
        std::string key = "text:" + (dot == std::string::npos ? "" : path.substr(dot)) + "\n" + text;
        return cached(std::move(key), [&] { return tokenize(text, path); });
    }
    Tokens get_source(const std::string& text, const std::string& path, bool whitespace, const std::string& identity) {
        auto dot = path.find_last_of('.');
        std::string key = "source:" + (dot == std::string::npos ? "" : path.substr(dot)) +
            (whitespace ? "\nspaces:" : "\nplain:") + identity;
        return cached(std::move(key), [&] { return tokenize(display_text(text, whitespace), path); });
    }
    size_t hits() const { return hits_; }
    size_t misses() const { return misses_; }
    size_t bytes() const { return cache_.bytes(); }
};

inline TokenCache& token_cache() {
    static TokenCache cache;
    return cache;
}

}
