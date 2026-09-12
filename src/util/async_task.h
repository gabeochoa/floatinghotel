#pragma once

#include <future>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace async_work {

enum class Priority { Foreground, Background };

class Executor {
    struct Job {
        std::function<void()> run;
        std::stop_source stop;
        bool cancellable;
    };
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Job> foreground_, background_;
    std::vector<std::thread> workers_;
    std::vector<std::optional<std::stop_source>> active_;
    size_t capacity_, backgroundLimit_, backgroundActive_ = 0;
    bool stopping_ = false;

    void work(size_t index) {
        for (;;) {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] {
                return stopping_ || !foreground_.empty() ||
                    (!background_.empty() && backgroundActive_ < backgroundLimit_);
            });
            if (stopping_ && foreground_.empty() && background_.empty()) return;
            bool background = foreground_.empty();
            auto& queue = background ? background_ : foreground_;
            Job job = std::move(queue.front());
            queue.pop_front();
            if (background) ++backgroundActive_;
            if (job.cancellable) active_[index] = job.stop;
            lock.unlock();
            job.run();
            lock.lock();
            active_[index].reset();
            if (background) --backgroundActive_;
            lock.unlock();
            ready_.notify_all();
        }
    }
public:
    explicit Executor(size_t threads = 4, size_t capacity = 64)
        : active_(std::max(size_t{1}, threads)), capacity_(capacity),
          backgroundLimit_(threads > 1 ? threads - 1 : 1) {
        for (size_t i = 0; i < active_.size(); ++i)
            workers_.emplace_back([this, i] { work(i); });
    }
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    ~Executor() { shutdown(); }
    bool submit(std::function<void()> run, std::stop_source stop,
                Priority priority, bool cancellable) {
        std::lock_guard lock(mutex_);
        const size_t queued = foreground_.size() + background_.size();
        const size_t limit = priority == Priority::Background
            ? capacity_ * 3 / 4 : capacity_;
        if (stopping_ || queued >= limit) return false;
        auto& queue = priority == Priority::Foreground ? foreground_ : background_;
        queue.push_back({std::move(run), std::move(stop), cancellable});
        ready_.notify_all();
        return true;
    }
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            for (auto& source : active_) if (source) source->request_stop();
            for (auto* queue : {&foreground_, &background_})
                for (auto& job : *queue) if (job.cancellable) job.stop.request_stop();
        }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }
};

inline Executor& executor() {
    static Executor pool;
    return pool;
}

template<class T>
class Task {
    std::future<T> future_;
    std::stop_source stop_{std::nostopstate};
public:
    Task() = default;
    Task(std::future<T> future, std::stop_source stop)
        : future_(std::move(future)), stop_(std::move(stop)) {}
    Task(Task&&) noexcept = default;
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            cancel();
            future_ = std::move(other.future_);
            stop_ = std::move(other.stop_);
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { cancel(); }
    bool valid() const { return future_.valid(); }
    void cancel() { stop_.request_stop(); }
    T get() { return future_.get(); }
    template<class Rep, class Period>
    std::future_status wait_for(std::chrono::duration<Rep, Period> duration) const {
        return future_.wait_for(duration);
    }
};

template<class Fn>
auto launch_on(Executor& pool, Fn fn, Priority priority = Priority::Foreground,
               std::invoke_result_t<Fn, std::stop_token> rejected = {},
               bool cancellable = true) {
    using Result = std::invoke_result_t<Fn, std::stop_token>;
    std::stop_source stop;
    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    auto run = [fn = std::move(fn), token = stop.get_token(), promise]() mutable {
        try { promise->set_value(fn(token)); }
        catch (...) { promise->set_exception(std::current_exception()); }
    };
    if (!pool.submit(std::move(run), stop, priority, cancellable))
        promise->set_value(std::move(rejected));
    return Task<Result>(std::move(future), std::move(stop));
}

template<class Fn>
auto launch(Fn fn, Priority priority = Priority::Foreground,
            std::invoke_result_t<Fn, std::stop_token> rejected = {},
            bool cancellable = true) {
    return launch_on(executor(), std::move(fn), priority, std::move(rejected), cancellable);
}

}
