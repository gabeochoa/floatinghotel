#pragma once

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

template<class Value> class SharedByteCache {
    struct Record {
        std::string key;
        Value value;
        size_t bytes;
        std::shared_ptr<size_t> live;
        Record(std::string k, Value v, size_t b, std::shared_ptr<size_t> l)
            : key(std::move(k)), value(std::move(v)), bytes(b), live(std::move(l)) { *live += bytes; }
        ~Record() { *live -= bytes; }
    };
    using Entry = std::shared_ptr<Record>;
    std::list<Entry> entries_;
    std::unordered_map<std::string_view, typename std::list<Entry>::iterator> index_;
    std::shared_ptr<size_t> live_ = std::make_shared<size_t>(0);
    size_t limit_;
    void evict() {
        index_.erase(entries_.back()->key);
        entries_.pop_back();
    }
public:
    explicit SharedByteCache(size_t limit) : limit_(limit) {
        index_.reserve(limit / (sizeof(Record) + 12 * sizeof(void*)) + 1);
    }
    std::shared_ptr<const Value> get(const std::string& key) {
        auto found = index_.find(key);
        if (found == index_.end()) return {};
        entries_.splice(entries_.begin(), entries_, found->second);
        const auto& record = *found->second;
        return std::shared_ptr<const Value>(record, &record->value);
    }
    std::shared_ptr<const Value> put(std::string key, Value value, size_t valueBytes) {
        if (auto found = index_.find(key); found != index_.end()) {
            auto entry = found->second;
            index_.erase(found);
            entries_.erase(entry);
        }
        key.shrink_to_fit();
        const size_t overhead = sizeof(Record) + 12 * sizeof(void*);
        if (valueBytes > limit_ || key.capacity() > limit_ || overhead > limit_ - valueBytes ||
            key.capacity() > limit_ - valueBytes - overhead) return {};
        const size_t charge = overhead + key.capacity() + valueBytes;
        while (!entries_.empty() && bytes() > limit_ - charge) evict();
        if (bytes() > limit_ - charge) return {};
        auto record = std::make_shared<Record>(std::move(key), std::move(value), charge, live_);
        entries_.push_front(record);
        index_.emplace(record->key, entries_.begin());
        return std::shared_ptr<const Value>(record, &record->value);
    }
    size_t bytes() const { return *live_ + index_.bucket_count() * sizeof(void*); }
};
