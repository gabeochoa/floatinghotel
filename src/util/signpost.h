#pragma once

// os_signpost intervals for Instruments drill-down (Time Profiler /
// os_signpost template). No-ops off macOS. To attach xctrace to a local
// build it must carry get-task-allow: `PERF=1 scripts/bundle.sh`, then
// `xctrace record --template 'Time Profiler' --launch -- output/FloatingHotel.app`.
#include <string>

#ifdef __APPLE__
#include <os/log.h>
#include <os/signpost.h>
#endif

namespace signpost {

#ifdef __APPLE__
inline os_log_t log_for(const char* category) {
    return os_log_create("com.floatinghotel", category);
}

class Interval {
public:
    // Signpost names must be string literals, so the semantic name rides
    // in the format arguments (Instruments shows it as the interval label).
    Interval(os_log_t log, const char* name, const std::string& detail)
        : log_(log), id_(os_signpost_id_generate(log)), name_(name) {
        os_signpost_interval_begin(log_, id_, "interval", "%s %s", name_, detail.c_str());
    }
    ~Interval() { os_signpost_interval_end(log_, id_, "interval", "%s", name_); }
    Interval(const Interval&) = delete;
    Interval& operator=(const Interval&) = delete;

private:
    os_log_t log_;
    os_signpost_id_t id_;
    const char* name_ = "";
};

inline void event(os_log_t log, const char* name, const std::string& detail) {
    os_signpost_event_emit(log, OS_SIGNPOST_ID_EXCLUSIVE, "event", "%s %s", name, detail.c_str());
}
#else
class Interval {
public:
    Interval(void*, const char*, const std::string&) {}
};
inline void* log_for(const char*) { return nullptr; }
inline void event(void*, const char*, const std::string&) {}
#endif

}  // namespace signpost
