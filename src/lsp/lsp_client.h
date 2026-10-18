#pragma once

// A language server over stdio. One reader thread turns the server's output
// into responses (fulfilling the request's future) and answers the few
// requests a server makes of its client; everything else is dropped.

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <sys/types.h>

namespace lsp {

// file:// URIs: percent-encode everything outside the unreserved set and '/'.
std::string uri_from_path(const std::string& path);
std::string path_from_uri(const std::string& uri);

// LSP positions count UTF-16 code units; the reader counts code points from 1.
int utf16_from_column(const std::string& line, int column);
int column_from_utf16(const std::string& line, int utf16);

class Client {
public:
    // Starts `command` in `root` and runs initialize/initialized. Null (with
    // `error` set) when the process cannot start or does not initialize within
    // `timeout`.
    static std::unique_ptr<Client> start(const std::vector<std::string>& command, const std::string& root,
                                         std::string& error, std::chrono::milliseconds timeout = std::chrono::seconds(20));
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    struct Pending {
        int id = 0;
        std::shared_future<nlohmann::json> result;  // {"result": ...} or {"error": "..."}
    };
    Pending request(const std::string& method, nlohmann::json params);
    void cancel(int id);
    void notify(const std::string& method, nlohmann::json params);
    bool alive() const { return alive_; }
    const nlohmann::json& capabilities() const { return capabilities_; }
    pid_t pid() const { return pid_; }

private:
    Client() = default;
    void send(const nlohmann::json& message);
    void read_loop();
    void fail_all(const std::string& why);

    pid_t pid_ = -1;
    int in_ = -1, out_ = -1;
    std::thread reader_;
    std::mutex write_, pending_mutex_;
    std::map<int, std::promise<nlohmann::json>> pending_;
    std::atomic<int> next_id_{1};
    std::atomic<bool> alive_{false};
    nlohmann::json capabilities_;
};

}  // namespace lsp
