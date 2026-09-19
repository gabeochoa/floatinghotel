#pragma once

#include <chrono>
#include <cstddef>

namespace reading_load {
using Clock = std::chrono::steady_clock;
inline double milliseconds(Clock::duration value) {
    return std::chrono::duration<double, std::milli>(value).count();
}
struct Frame {
    double layoutMs = 0, splitMs = 0;
    size_t splitPreparedPairs = 0, splitEmittedPairs = 0, splitComparisons = 0;
};
inline Frame frame;
inline double totalLayoutMs = 0;

struct Trace {
    Clock::time_point submitted{}, started{}, finished{}, published{};
    double validationMs = 0, cacheMs = 0, readMs = 0, decodeMs = 0, sharedWaitMs = 0;
    double gitLockMs = 0, gitProcessMs = 0, publicationMs = 0;
    size_t gitCommands = 0;
    bool cacheHit = false;
};
struct Phase {
    double& elapsed;
    Clock::time_point start = Clock::now();
    ~Phase() { elapsed += milliseconds(Clock::now() - start); }
};
template<class F> auto measure(double& elapsed, F&& read) {
    Phase phase{elapsed};
    return read();
}
struct Publishing {
    Trace& trace;
    Clock::time_point start = Clock::now();
    Publishing(Trace& target, const Trace& value) : trace(target) { trace = value; }
    ~Publishing() {
        trace.published = Clock::now();
        trace.publicationMs = milliseconds(trace.published - start);
    }
};
}
