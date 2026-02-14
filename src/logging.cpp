#include "logging.h"

#include <chrono>

namespace logging {

ScopedTimer::ScopedTimer(const char* n) : name(n) {
    startTime = std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now().time_since_epoch())
                    .count();
}

ScopedTimer::~ScopedTimer() {
    double now = std::chrono::duration<double>(
                     std::chrono::high_resolution_clock::now().time_since_epoch())
                     .count();
    double elapsed = (now - startTime) * 1000.0;
    info("%s took %.3f ms", name, elapsed);
}

}  // namespace logging
