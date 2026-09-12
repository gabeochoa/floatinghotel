#pragma once

#include <atomic>
#include <concepts>
#include <filesystem>
#include <limits.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../vendor/afterhours/src/logging.h"

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace platform {

struct FileWatchEvent {
    std::string path;
    bool mustRescan = false;
};

template <typename T>
concept FileWatcherBackend = requires(T& t, const std::string& path) {
    { t.watch(path) } -> std::same_as<void>;
    { t.stop() } -> std::same_as<void>;
    { t.poll_changed() } -> std::same_as<bool>;
};

// =============================================================================
// Apple — FSEvents
// =============================================================================
#ifdef __APPLE__

class FSEventsWatcher {
public:
    FSEventsWatcher() = default;

    ~FSEventsWatcher() { stop(); }

    FSEventsWatcher(const FSEventsWatcher&) = delete;
    FSEventsWatcher& operator=(const FSEventsWatcher&) = delete;

    void watch(const std::string& path) {
        watch_many(std::vector<std::string>{path});
    }

    void watch_many(const std::vector<std::string>& paths) {
        stop();

        changed_.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            events_.clear();
        }

        std::vector<CFStringRef> cf_paths;
        std::vector<const void*> values;
        for (const auto& path : paths) {
            std::error_code ec;
            auto canon = std::filesystem::canonical(path, ec);
            std::string real_path = ec ? path : canon.string();
            CFStringRef cf_path = CFStringCreateWithCString(
                kCFAllocatorDefault, real_path.c_str(), kCFStringEncodingUTF8);
            if (!cf_path) continue;
            cf_paths.push_back(cf_path);
            values.push_back(cf_path);
        }

        if (values.empty()) return;
        CFArrayRef cf_array = CFArrayCreate(
            kCFAllocatorDefault,
            values.data(), static_cast<CFIndex>(values.size()),
            &kCFTypeArrayCallBacks);

        FSEventStreamContext ctx{};
        ctx.info = this;

        stream_ = FSEventStreamCreate(
            kCFAllocatorDefault,
            &FSEventsWatcher::fs_callback,
            &ctx,
            cf_array,
            kFSEventStreamEventIdSinceNow,
            0.5,
            kFSEventStreamCreateFlagUseCFTypes |
                kFSEventStreamCreateFlagFileEvents);

        CFRelease(cf_array);
        for (auto* cf_path : cf_paths) CFRelease(cf_path);

        if (!stream_) {
            log_warn("FSEventsWatcher: failed to create stream");
            return;
        }

        FSEventStreamRef stream = stream_;
        finished_.store(false, std::memory_order_release);
        run_loop_thread_ = std::thread([this, stream] {
            // CFRunLoopGetCurrent() is unowned and the loop is freed when this
            // thread exits. stop() reads finished_ and then calls
            // CFRunLoopStop(rl); if the thread exits in between, that is a
            // CFRunLoopStop on a dead object (EXC_BREAKPOINT inside CF, seen
            // under load in the E2E batch). Retain it so stop() owns a live
            // reference until after join().
            CFRunLoopRef rl = CFRunLoopGetCurrent();
            CFRetain(rl);
            run_loop_.store(rl, std::memory_order_release);

            FSEventStreamScheduleWithRunLoop(
                stream, rl, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream);
            CFRunLoopRun();

            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            finished_.store(true, std::memory_order_release);
        });
    }

    void stop() {
        if (!stream_) return;

        if (run_loop_thread_.joinable()) {
            // Keep asking the run loop to stop until the thread actually exits.
            // A single CFRunLoopStop can race the thread: if it lands before
            // CFRunLoopRun() has entered, it's a no-op and the loop would run
            // forever, hanging join() (a real freeze). Retrying until finished_
            // is set closes that window (once the loop enters, a later stop
            // takes). Bounded so a pathological case can't spin indefinitely.
            for (int i = 0; i < 2000 && !finished_.load(std::memory_order_acquire);
                 ++i) {
                CFRunLoopRef rl = run_loop_.load(std::memory_order_acquire);
                if (rl) CFRunLoopStop(rl);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            run_loop_thread_.join();
        }
        if (CFRunLoopRef rl = run_loop_.exchange(nullptr, std::memory_order_acq_rel)) {
            CFRelease(rl);
        }

        FSEventStreamRelease(stream_);
        stream_ = nullptr;
    }

    bool poll_changed() {
        return changed_.exchange(false, std::memory_order_acq_rel);
    }

    std::vector<FileWatchEvent> poll_events() {
        changed_.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(events_mutex_);
        auto events = std::move(events_);
        events_.clear();
        return events;
    }

private:
    static void fs_callback(
        ConstFSEventStreamRef /*stream*/,
        void* context,
        size_t num_events,
        void* event_paths,
        const FSEventStreamEventFlags* event_flags,
        const FSEventStreamEventId* /*event_ids*/) {
        auto* self = static_cast<FSEventsWatcher*>(context);
        auto paths = static_cast<CFArrayRef>(event_paths);
        std::lock_guard<std::mutex> lock(self->events_mutex_);
        for (size_t i = 0; i < num_events; ++i) {
            auto cf_path = static_cast<CFStringRef>(CFArrayGetValueAtIndex(paths, static_cast<CFIndex>(i)));
            char buffer[PATH_MAX];
            std::string path;
            if (CFStringGetCString(cf_path, buffer, sizeof(buffer), kCFStringEncodingUTF8))
                path = buffer;
            bool mustRescan = event_flags &&
                ((event_flags[i] & kFSEventStreamEventFlagMustScanSubDirs) ||
                 (event_flags[i] & kFSEventStreamEventFlagUserDropped) ||
                 (event_flags[i] & kFSEventStreamEventFlagKernelDropped));
            self->events_.push_back({path, mustRescan});
        }
        self->changed_.store(true, std::memory_order_release);
    }

    std::atomic<bool> changed_{false};
    std::atomic<bool> finished_{false}; // set by the thread after CFRunLoopRun
    FSEventStreamRef stream_{nullptr};
    std::atomic<CFRunLoopRef> run_loop_{nullptr};
    std::thread run_loop_thread_;
    std::mutex events_mutex_;
    std::vector<FileWatchEvent> events_;
};

#pragma clang diagnostic pop

static_assert(FileWatcherBackend<FSEventsWatcher>);
using FileWatcher = FSEventsWatcher;

// =============================================================================
// Fallback — no-op stub
// =============================================================================
#else

class NullWatcher {
public:
    void watch(const std::string&) {}
    void watch_many(const std::vector<std::string>&) {}
    void stop() {}
    bool poll_changed() { return false; }
    std::vector<FileWatchEvent> poll_events() { return {}; }
};

static_assert(FileWatcherBackend<NullWatcher>);
using FileWatcher = NullWatcher;

#endif

} // namespace platform
