#pragma once

#include <atomic>
#include <concepts>
#include <filesystem>
#include <string>
#include <thread>

#include "../../vendor/afterhours/src/logging.h"

namespace platform {

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

#include <CoreServices/CoreServices.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

class FSEventsWatcher {
public:
    FSEventsWatcher() = default;

    ~FSEventsWatcher() { stop(); }

    FSEventsWatcher(const FSEventsWatcher&) = delete;
    FSEventsWatcher& operator=(const FSEventsWatcher&) = delete;

    void watch(const std::string& path) {
        stop();

        watched_path_ = path;
        changed_.store(false, std::memory_order_relaxed);

        // Resolve symlinks — FSEvents requires canonical paths
        // (e.g. /tmp -> /private/tmp on macOS)
        std::error_code ec;
        auto canon = std::filesystem::canonical(path, ec);
        std::string real_path = ec ? path : canon.string();

        CFStringRef cf_path = CFStringCreateWithCString(
            kCFAllocatorDefault, real_path.c_str(), kCFStringEncodingUTF8);
        CFArrayRef paths = CFArrayCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const void**>(&cf_path), 1,
            &kCFTypeArrayCallBacks);

        FSEventStreamContext ctx{};
        ctx.info = this;

        stream_ = FSEventStreamCreate(
            kCFAllocatorDefault,
            &FSEventsWatcher::fs_callback,
            &ctx,
            paths,
            kFSEventStreamEventIdSinceNow,
            0.5, // latency in seconds — coalesces rapid writes
            kFSEventStreamCreateFlagUseCFTypes |
                kFSEventStreamCreateFlagFileEvents);

        CFRelease(paths);
        CFRelease(cf_path);

        if (!stream_) {
            log_warn("FSEventsWatcher: failed to create stream");
            return;
        }

        run_loop_thread_ = std::thread([this] {
            run_loop_ = CFRunLoopGetCurrent();
            FSEventStreamScheduleWithRunLoop(
                stream_, run_loop_, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream_);
            CFRunLoopRun();
        });
    }

    void stop() {
        if (stream_) {
            FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        if (run_loop_) {
            CFRunLoopStop(run_loop_);
            run_loop_ = nullptr;
        }
        if (run_loop_thread_.joinable()) {
            run_loop_thread_.join();
        }
    }

    bool poll_changed() {
        return changed_.exchange(false, std::memory_order_acq_rel);
    }

private:
    static void fs_callback(
        ConstFSEventStreamRef /*stream*/,
        void* context,
        size_t /*num_events*/,
        void* /*event_paths*/,
        const FSEventStreamEventFlags* /*event_flags*/,
        const FSEventStreamEventId* /*event_ids*/) {
        auto* self = static_cast<FSEventsWatcher*>(context);
        self->changed_.store(true, std::memory_order_release);
    }

    std::string watched_path_;
    std::atomic<bool> changed_{false};
    FSEventStreamRef stream_{nullptr};
    CFRunLoopRef run_loop_{nullptr};
    std::thread run_loop_thread_;
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
    void stop() {}
    bool poll_changed() { return false; }
};

static_assert(FileWatcherBackend<NullWatcher>);
using FileWatcher = NullWatcher;

#endif

} // namespace platform
