#pragma once

#include <chrono>
#include <optional>

namespace loading_feedback {

struct Delay {
    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> started;
    void restart(Clock::time_point now = Clock::now()) { started = now; }
    bool visible(bool pending, Clock::time_point now = Clock::now()) const {
        return pending && started && now - *started >= std::chrono::milliseconds(150);
    }
};

}
