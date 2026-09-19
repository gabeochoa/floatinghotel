#pragma once

#include <algorithm>
#include <chrono>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <stop_token>
#include <vector>

namespace async_work {

template<class Key, class Value>
class SharedRead {
    struct Entry {
        Key key;
        std::promise<Value> promise;
        std::shared_future<Value> result = promise.get_future().share();
        std::stop_source stop;
        bool accepting = true;
        std::vector<std::stop_token> callers;
        explicit Entry(const Key& value) : key(value) {}
    };
    std::mutex mutex_;
    std::list<std::shared_ptr<Entry>> entries_;
    size_t shared_ = 0;
public:
    size_t shared_count() {
        std::lock_guard lock(mutex_);
        return shared_;
    }

    template<class Load>
    Value run(const Key& key, std::stop_token caller, Value cancelled, Load load) {
        if (caller.stop_requested()) return cancelled;
        std::shared_ptr<Entry> entry;
        bool owner = false;
        {
            std::lock_guard lock(mutex_);
            auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& item) {
                return item->key == key && item->accepting;
            });
            if (found != entries_.end()) { entry = *found; ++shared_; }
            else if (entries_.size() < 64) {
                entry = std::make_shared<Entry>(key);
                entries_.push_back(entry);
                owner = true;
            }
            if (entry) entry->callers.push_back(caller);
        }
        if (!entry) return load(caller);
        std::stop_callback cancel(caller, [this, entry] {
            bool unused;
            {
                std::lock_guard lock(mutex_);
                unused = std::all_of(entry->callers.begin(), entry->callers.end(),
                    [](auto token) { return token.stop_requested(); });
                if (unused) entry->accepting = false;
            }
            if (unused) entry->stop.request_stop();
        });
        if (owner) {
            try { entry->promise.set_value(load(entry->stop.get_token())); }
            catch (...) { entry->promise.set_exception(std::current_exception()); }
            std::lock_guard lock(mutex_);
            entries_.remove(entry);
        } else {
            while (entry->result.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
                if (caller.stop_requested()) return cancelled;
        }
        if (caller.stop_requested()) return cancelled;
        return entry->result.get();
    }
};

}
