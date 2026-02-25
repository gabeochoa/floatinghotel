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

        changed_.store(false, std::memory_order_relaxed);

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
            0.5,
            kFSEventStreamCreateFlagUseCFTypes |
                kFSEventStreamCreateFlagFileEvents);

        CFRelease(paths);
        CFRelease(cf_path);

        if (!stream_) {
            log_warn("FSEventsWatcher: failed to create stream");
            return;
        }

        FSEventStreamRef stream = stream_;
        run_loop_thread_ = std::thread([this, stream] {
            CFRunLoopRef rl = CFRunLoopGetCurrent();
            run_loop_.store(rl, std::memory_order_release);

            FSEventStreamScheduleWithRunLoop(
                stream, rl, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream);
            CFRunLoopRun();

            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
        });
    }

    void stop() {
        if (!stream_) return;

        if (run_loop_thread_.joinable()) {
            while (!run_loop_.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            CFRunLoopStop(run_loop_.load(std::memory_order_acquire));
            run_loop_thread_.join();
        }
        run_loop_.store(nullptr, std::memory_order_relaxed);

        FSEventStreamRelease(stream_);
        stream_ = nullptr;
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

    std::atomic<bool> changed_{false};
    FSEventStreamRef stream_{nullptr};
    std::atomic<CFRunLoopRef> run_loop_{nullptr};
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
