#pragma once

#include "../ecs/components.h"
#include <list>
#include <mutex>
#include <optional>

namespace git {

inline constexpr size_t commitPatchCacheBudget = 32 * 1024 * 1024;

struct CommitPatchKey {
    std::string repository;
    std::string commonDirectory;
    std::string commit;
    std::string parent;
    int context = 3;
    bool ignoreWhitespace = false;
    bool firstParent = false;
    bool operator==(const CommitPatchKey&) const = default;
};

inline size_t commit_patch_owned_bytes(const CommitPatchKey& key, const ecs::CommitPatch& patch) {
    size_t bytes = sizeof(key) + sizeof(patch) + sizeof(size_t) + 2 * sizeof(void*);
    auto text = [&](const std::string& value) { bytes += value.capacity() + 1; };
    for (const auto* value : {&key.repository, &key.commonDirectory, &key.commit, &key.parent,
             &patch.metadata, &patch.error, &patch.resolvedCommit, &patch.resolvedParent}) text(*value);
    bytes += patch.files.capacity() * sizeof(ecs::FileDiff);
    for (const auto& file : patch.files) {
        for (const auto* value : {&file.filePath, &file.oldPath, &file.oldMode, &file.newMode,
                &file.oldObject, &file.newObject}) text(*value);
        bytes += file.hunks.capacity() * sizeof(ecs::DiffHunk);
        for (const auto& hunk : file.hunks) {
            text(hunk.header);
            bytes += hunk.lines.capacity() * sizeof(std::string);
            for (const auto& line : hunk.lines) text(line);
            bytes += (hunk.noNewline.size() + hunk.movedLines.size()) * (sizeof(size_t) + 4 * sizeof(void*));
        }
    }
    return bytes;
}

class CommitPatchCache {
    struct Entry {
        CommitPatchKey key;
        ecs::CommitPatch patch;
        size_t bytes = 0;
    };
    mutable std::mutex mutex_;
    std::list<Entry> entries_;
    size_t hits_ = 0, misses_ = 0;
    size_t budget_;
    size_t bytes_ = 0;
public:
    explicit CommitPatchCache(size_t budget = commitPatchCacheBudget) : budget_(budget) {}

    std::optional<ecs::CommitPatch> get(const CommitPatchKey& key) {
        std::lock_guard lock(mutex_);
        auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.key == key; });
        if (found == entries_.end()) { ++misses_; return {}; }
        ++hits_;
        entries_.splice(entries_.begin(), entries_, found);
        return entries_.front().patch;
    }

    bool put(const CommitPatchKey& key, const ecs::CommitPatch& patch) {
        if (!patch.error.empty() || commit_patch_owned_bytes(key, patch) > budget_) return false;
        Entry entry{key, patch};
        entry.bytes = commit_patch_owned_bytes(entry.key, entry.patch);
        if (entry.bytes > budget_) return false;
        std::lock_guard lock(mutex_);
        auto previous = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& cached) { return cached.key == key; });
        if (previous != entries_.end()) { bytes_ -= previous->bytes; entries_.erase(previous); }
        while (!entries_.empty() && bytes_ + entry.bytes > budget_) {
            bytes_ -= entries_.back().bytes;
            entries_.pop_back();
        }
        bytes_ += entry.bytes;
        entries_.push_front(std::move(entry));
        return true;
    }

    std::pair<size_t, size_t> activity() const {
        std::lock_guard lock(mutex_);
        return {hits_, misses_};
    }

    size_t bytes() const {
        std::lock_guard lock(mutex_);
        return bytes_;
    }
};

inline CommitPatchCache& commit_patch_cache() {
    static CommitPatchCache cache;
    return cache;
}

}
