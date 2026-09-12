#pragma once

#include <functional>
#include <future>
#include <string>
#include <stop_token>
#include <vector>

struct ProcessResult {
    std::string stdout_str;
    std::string stderr_str;
    int exit_code = -1;
    bool cancelled = false;
    bool success() const { return exit_code == 0; }
};

// Synchronous. timeout_ms > 0 kills the child and returns a failure (exit_code
// -1, stderr "timed out after Ns") if it hasn't finished in time; 0 waits
// forever (the historical behaviour).
ProcessResult run_process(const std::string& working_dir,
                          const std::vector<std::string>& args,
                          int timeout_ms = 0, std::stop_token stop = {});

// Asynchronous -- for slow git operations (push, pull, fetch)
std::future<ProcessResult> run_process_async(
    const std::string& working_dir, const std::vector<std::string>& args,
    std::function<void(const std::string&)> on_output = nullptr);
