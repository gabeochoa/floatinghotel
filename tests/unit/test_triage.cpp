#include "test_framework.h"
#include "../../src/git/log_redaction.h"
#include "../../src/util/shared_read.h"
#include "../../src/util/history_selection.h"
#include "../../src/util/async_task.h"
#include <atomic>
#include <thread>

TEST(shared_reads_join_one_loader_and_keep_other_callers_alive) {
    async_work::SharedRead<std::string, int> reads;
    std::promise<void> started, finish;
    auto ready = finish.get_future().share();
    std::atomic<int> loads{0};
    std::stop_source firstStop;
    auto first = std::async(std::launch::async, [&] {
        return reads.run("revision-a", firstStop.get_token(), -1, [&](auto stop) {
            ++loads;
            started.set_value();
            ready.wait();
            return stop.stop_requested() ? -2 : 42;
        });
    });
    started.get_future().wait();
    auto second = std::async(std::launch::async, [&] {
        return reads.run("revision-a", {}, -1, [&](auto) { ++loads; return 100; });
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (reads.shared_count() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    firstStop.request_stop();
    finish.set_value();
    ASSERT_EQ(first.get(), -1);
    ASSERT_EQ(second.get(), 42);
    ASSERT_EQ(loads.load(), 1);
    ASSERT_EQ(reads.run("revision-a", {}, -1, [](auto) { return 43; }), 43);
}

TEST(shared_reads_do_not_join_different_keys_and_cancel_unused_work) {
    async_work::SharedRead<std::string, int> reads;
    std::promise<void> started;
    std::stop_source source;
    auto running = std::async(std::launch::async, [&] {
        return reads.run("first", source.get_token(), -1, [&](auto stop) {
            started.set_value();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            return stop.stop_requested() ? 7 : 8;
        });
    });
    started.get_future().wait();
    ASSERT_EQ(reads.run("second", {}, -1, [](auto) { return 9; }), 9);
    source.request_stop();
    ASSERT_EQ(running.get(), -1);
    ASSERT_EQ(reads.shared_count(), 0u);
    ASSERT_EQ(reads.run("first", {}, -1, [](auto) { return 10; }), 10);
}

TEST(log_redaction_preserves_useful_output_and_removes_credentials) {
    auto safe = git::redact_git_log({"git", "push", "https://user:fixture-secret@example.test/repo",
        "--token", "fixture-token", "-c", "http.extraHeader=Authorization: Bearer fixture-bearer"},
        "remote rejected fixture-secret and fixture-token", "authentication failed for fixture-bearer");
    for (const auto& text : {safe.command, safe.output, safe.error}) {
        ASSERT_TRUE(text.find("fixture-secret") == std::string::npos);
        ASSERT_TRUE(text.find("fixture-token") == std::string::npos);
        ASSERT_TRUE(text.find("fixture-bearer") == std::string::npos);
    }
    ASSERT_TRUE(safe.command.find("example.test/repo") != std::string::npos);
    ASSERT_TRUE(safe.output.find("remote rejected") != std::string::npos);
    ASSERT_EQ(git::redact_log_text("https://example.test/repo?access_token=secret&name=ok"),
        std::string("https://example.test/repo?access_token=<redacted>&name=ok"));
    ASSERT_EQ(git::redact_log_text("normal diff and status output"), std::string("normal diff and status output"));
}

TEST(log_redaction_handles_case_multiline_and_inline_secrets) {
    auto safe = git::redact_git_log({"git", "--password=private123", "--api-key", "key123"},
        "private123 key123\nAUTHORIZATION: Basic credential\nkeep this", "https://secret@example.test/a");
    ASSERT_TRUE(safe.command.find("private123") == std::string::npos);
    ASSERT_TRUE(safe.output.find("key123") == std::string::npos);
    ASSERT_TRUE(safe.output.find("credential") == std::string::npos);
    ASSERT_TRUE(safe.output.find("keep this") != std::string::npos);
    ASSERT_TRUE(safe.error.find("secret") == std::string::npos);
}

TEST(history_selection_tracks_hashes_and_copies_visible_order) {
    struct Commit { std::string hash; };
    reading::HistorySelection selection;
    const std::vector<std::string> visible{"new", "middle", "old"};
    selection.select(visible, "new", false, false);
    selection.select(visible, "old", true, false);
    const std::vector<Commit> commits{{"new"}, {"middle"}, {"old"}};
    ASSERT_TRUE(selection.contiguous(commits));
    auto selected = selection.visible(commits);
    ASSERT_EQ(selected[0].hash, std::string("new"));
    ASSERT_EQ(selected[2].hash, std::string("old"));
    selection.select(visible, "middle", false, true);
    ASSERT_FALSE(selection.contiguous(commits));
    const std::vector<Commit> filtered{{"old"}};
    ASSERT_EQ(selection.visible(filtered).size(), 1u);
    ASSERT_EQ(selection.hashes.size(), 2u);
    selection.select(visible, "missing", true, false);
    ASSERT_EQ(selection.hashes.size(), 2u);
}

TEST(cancellation_remains_pending_until_worker_acknowledges_it) {
    async_work::Executor executor(1, 8);
    std::promise<void> started, finish;
    auto gate = finish.get_future().share();
    auto task = async_work::launch_on(executor, [&](std::stop_token stop) {
        started.set_value();
        gate.wait();
        return stop.stop_requested();
    });
    started.get_future().wait();
    ASSERT_TRUE(task.can_cancel());
    task.cancel();
    ASSERT_TRUE(task.cancellation_requested());
    ASSERT_TRUE(task.valid());
    ASSERT_EQ(task.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    finish.set_value();
    ASSERT_TRUE(task.get());
    auto write = async_work::launch_on(executor, [](std::stop_token stop) { return stop.stop_requested(); }, async_work::Priority::Foreground, false, false);
    ASSERT_FALSE(write.can_cancel());
    write.cancel();
    ASSERT_FALSE(write.get());
}

TEST(log_capture_bounds_large_output_without_leaking_partial_secret_lines) {
    const auto huge = std::string(2 * 1024 * 1024, 'x');
    ASSERT_EQ(git::bounded_log_output(huge), std::string("[command log output limit reached]"));
    auto captured = git::bounded_log_output("safe first line\n" + huge);
    ASSERT_TRUE(captured.starts_with("safe first line\n"));
    ASSERT_TRUE(captured.size() < 100);
    auto redacted = git::redact_log_text("Authorization: Bearer " + huge);
    ASSERT_EQ(redacted, std::string("Authorization: <redacted>"));
}

int main() { RUN_ALL_TESTS(); }
