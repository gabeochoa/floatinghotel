#pragma once

#include <list>
#include <string>
#include <unordered_map>
#include <utility>

template<class Value>
class ByteCache {
    struct Entry { std::string key; Value value; size_t bytes; };
    std::list<Entry> entries_;
    std::unordered_map<std::string, typename std::list<Entry>::iterator> index_;
    size_t limit_, bytes_ = 0;
public:
    explicit ByteCache(size_t limit) : limit_(limit) {}
    ByteCache(const ByteCache&) = delete;
    ByteCache& operator=(const ByteCache&) = delete;
    const Value* get(const std::string& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return nullptr;
        entries_.splice(entries_.begin(), entries_, it->second);
        return &it->second->value;
    }
    bool put(std::string key, Value value, size_t valueBytes) {
        if (auto it = index_.find(key); it != index_.end()) {
            bytes_ -= it->second->bytes;
            entries_.erase(it->second);
            index_.erase(it);
        }
        const size_t overhead = sizeof(Entry) + sizeof(std::string) + 10 * sizeof(void*);
        if (valueBytes > limit_ || key.capacity() > limit_ / 2 ||
            overhead > limit_ - valueBytes || key.capacity() * 2 > limit_ - valueBytes - overhead) return false;
        const size_t bytes = overhead + key.capacity() * 2 + valueBytes;
        while (!entries_.empty() && this->bytes() > limit_ - bytes) {
            auto& oldest = entries_.back();
            bytes_ -= oldest.bytes;
            index_.erase(oldest.key);
            entries_.pop_back();
        }
        entries_.push_front({std::move(key), std::move(value), bytes});
        index_.emplace(entries_.front().key, entries_.begin());
        bytes_ += bytes;
        while (!entries_.empty() && this->bytes() > limit_) {
            auto& oldest = entries_.back();
            bytes_ -= oldest.bytes;
            index_.erase(oldest.key);
            entries_.pop_back();
        }
        return !entries_.empty();
    }
    size_t bytes() const { return bytes_ + index_.bucket_count() * sizeof(void*); }
    size_t size() const { return entries_.size(); }
};
