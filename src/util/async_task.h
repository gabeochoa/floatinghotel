#pragma once

#include <future>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace async_work {

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
auto launch(Fn fn) {
    using Result = std::invoke_result_t<Fn, std::stop_token>;
    std::stop_source stop;
    std::packaged_task<Result()> task([fn = std::move(fn), token = stop.get_token()]() mutable {
        return fn(token);
    });
    auto future = task.get_future();
    std::thread(std::move(task)).detach();
    return Task<Result>(std::move(future), std::move(stop));
}

}
