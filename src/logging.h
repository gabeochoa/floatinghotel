#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace logging {

enum class Level { Debug, Info, Warning, Error };

inline Level g_level = Level::Info;

inline void setLevel(Level level) { g_level = level; }

inline void debug(const char* fmt, ...) {
    if (g_level > Level::Debug) return;
    std::printf("[DEBUG] ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

inline void info(const char* fmt, ...) {
    if (g_level > Level::Info) return;
    std::printf("[INFO] ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

inline void warning(const char* fmt, ...) {
    if (g_level > Level::Warning) return;
    std::printf("[WARNING] ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

inline void error(const char* fmt, ...) {
    std::printf("[ERROR] ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

struct ScopedTimer {
    const char* name;
    double startTime;

    ScopedTimer(const char* n);
    ~ScopedTimer();
};

}  // namespace logging

#define LOG_DEBUG(...) logging::debug(__VA_ARGS__)
#define LOG_INFO(...) logging::info(__VA_ARGS__)
#define LOG_WARNING(...) logging::warning(__VA_ARGS__)
#define LOG_ERROR(...) logging::error(__VA_ARGS__)
#define SCOPED_TIMER_CONCAT_INNER(a, b) a##b
#define SCOPED_TIMER_CONCAT(a, b) SCOPED_TIMER_CONCAT_INNER(a, b)
#define SCOPED_TIMER(name) \
    logging::ScopedTimer SCOPED_TIMER_CONCAT(_timer_, __COUNTER__)(name)
