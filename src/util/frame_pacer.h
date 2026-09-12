#pragma once

#include <algorithm>
#include <chrono>
#include <future>

namespace frame_pacer {

template<class Task>
bool in_flight(const Task& task) {
    return task.valid() && task.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

struct Decision {
    bool render = true;
    bool sleep = false;
};

struct FramePacer {
    bool enabled = true;
    int activeFrames = 3;
    int idleSkipStreak = 0;
    int rendered = 0;
    int skipped = 0;
    int maxIdleSkips = 12;

    void reset_stats() {
        rendered = 0;
        skipped = 0;
        idleSkipStreak = 0;
        activeFrames = 3;
    }

    Decision decide(bool force, bool activity, bool pendingWork) {
        if (!enabled || force || activity || pendingWork) {
            activeFrames = 3;
            idleSkipStreak = 0;
            ++rendered;
            return {true, false};
        }
        if (activeFrames > 0) {
            --activeFrames;
            idleSkipStreak = 0;
            ++rendered;
            return {true, false};
        }
        if (idleSkipStreak >= maxIdleSkips) {
            idleSkipStreak = 0;
            ++rendered;
            return {true, false};
        }
        ++idleSkipStreak;
        ++skipped;
        return {false, true};
    }
};

}
