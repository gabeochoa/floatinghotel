#pragma once

#include "async_task.h"
#include "loading_feedback.h"
#include <string>

namespace commit_prefetch {

struct Candidate {
    std::string repository;
    std::string commit;
    int context = 3;
    bool ignoreWhitespace = false;
    bool operator==(const Candidate&) const = default;
};

struct State {
    std::optional<Candidate> candidate;
    loading_feedback::Delay intent;
    async_work::Task<bool> future;
    bool attempted = false;
    bool cancelling = false;
    size_t submitted = 0, completed = 0, cancelled = 0, discarded = 0;

    bool observe(std::optional<Candidate> next, loading_feedback::Delay::Clock::time_point now = loading_feedback::Delay::Clock::now()) {
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { if (future.get()) ++completed; else ++discarded; } catch (...) { ++discarded; }
            future = {};
            cancelling = false;
        }
        if (candidate != next) {
            if (future.valid() && !cancelling) { future.cancel(); cancelling = true; ++cancelled; }
            candidate = std::move(next);
            intent.restart(now);
            attempted = false;
        }
        return candidate && !attempted && !future.valid() && intent.visible(true, now);
    }
};

}
