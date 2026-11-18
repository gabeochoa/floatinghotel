#pragma once

#include "async_task.h"
#include "reading_workspace.h"
#include "signpost.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace async_slot {

enum class Phase { Idle, Pending, Ready, Failed };

inline std::string_view name(Phase phase) {
    switch (phase) {
        case Phase::Idle: return "Idle";
        case Phase::Pending: return "Pending";
        case Phase::Ready: return "Ready";
        case Phase::Failed: return "Failed";
    }
    return "Idle";
}

template <class T, class Key = std::string>
struct Slot {
    Phase phase = Phase::Idle;
    Key key{};
    reading::RequestStamp stamp{};
    async_work::Task<T> task;
    std::string error;

    bool busy() const { return phase == Phase::Pending; }

    void request(Key nextKey, reading::RequestStamp nextStamp, async_work::Task<T> nextTask) {
        if (task.valid()) task.cancel();
        task = std::move(nextTask);
        key = std::move(nextKey);
        stamp = std::move(nextStamp);
        error.clear();
        set_phase(Phase::Pending);
    }

    // Unstamped variant for work whose staleness is decided by the caller
    // at completion (network ops gate on the repository still matching),
    // not by a RequestStamp.
    void request(Key nextKey, async_work::Task<T> nextTask) {
        request(std::move(nextKey), reading::RequestStamp{}, std::move(nextTask));
    }

    bool ready() const {
        return phase == Phase::Pending && task.valid() &&
            task.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    template <class Repo>
    std::optional<T> poll(const Repo& repo) {
        if (phase != Phase::Pending) return std::nullopt;
        if (!task.valid()) {
            set_phase(Phase::Idle);
            return std::nullopt;
        }
        if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        if (!accepts(repo)) {
            task = async_work::Task<T>{};
            set_phase(Phase::Idle);
            return std::nullopt;
        }
        try {
            T value = task.get();
            set_phase(Phase::Ready);
            return value;
        } catch (const std::exception& e) {
            error = e.what();
            set_phase(Phase::Failed);
            return std::nullopt;
        } catch (...) {
            error = "unknown error";
            set_phase(Phase::Failed);
            return std::nullopt;
        }
    }

    // Consumes a ready task without the stamp check in poll(), for slots
    // requested unstamped. A thrown task still lands in Failed.
    std::optional<T> poll_unstamped() {
        if (phase != Phase::Pending) return std::nullopt;
        if (!task.valid()) {
            set_phase(Phase::Idle);
            return std::nullopt;
        }
        if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        try {
            T value = task.get();
            set_phase(Phase::Ready);
            return value;
        } catch (const std::exception& e) {
            error = e.what();
            set_phase(Phase::Failed);
            return std::nullopt;
        } catch (...) {
            error = "unknown error";
            set_phase(Phase::Failed);
            return std::nullopt;
        }
    }

    // Land a consumed result as Failed (e.g. a GitResult whose stderr
    // reports failure, or a reader result carrying an error string):
    // the task is already consumed, the error is retained, and the key
    // stays so callers can cache the failure for that key.
    void fail(std::string message) {
        task = async_work::Task<T>{};
        error = std::move(message);
        set_phase(Phase::Failed);
    }

    void cancel() {
        if (task.valid()) task.cancel();
        task = async_work::Task<T>{};
        error.clear();
        set_phase(Phase::Idle);
    }

    void invalidate() {
        if (task.valid()) task.cancel();
        task = async_work::Task<T>{};
        key = Key{};
        stamp = reading::RequestStamp{};
        error.clear();
        set_phase(Phase::Idle);
    }

private:
    template <class Repo>
    bool accepts(const Repo& repo) const {
        if (stamp.repository != repo.repoPath) return false;
        if (!reading::same_document(stamp.document, repo.workspace().location())) return false;
        if (stamp.generation != repo.workspace().generation()) return false;
        if (stamp.dataGeneration != repo.dataGeneration) return false;
        if constexpr (std::is_same_v<Key, std::string>) {
            if (!stamp.key.empty() && stamp.key != key) return false;
        }
        return true;
    }

    void set_phase(Phase next) {
        if (phase == next) return;
        const Phase old = phase;
        phase = next;
        static auto log = signpost::log_for("async_slot");
        signpost::event(log, "async_slot_transition",
                        std::string(name(old)) + " -> " + std::string(name(next)));
    }
};

}  // namespace async_slot
