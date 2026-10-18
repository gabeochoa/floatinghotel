#include "lsp_client.h"

#include "json_rpc.h"

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace lsp {

std::string uri_from_path(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string path_from_uri(const std::string& uri) {
    if (uri.rfind("file://", 0) != 0) return {};
    std::string out;
    for (size_t i = 7; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
            out += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += uri[i];
    }
    return out;
}

// Code points from `line` until `stop` says to end; returns UTF-16 units and
// code points consumed. A lead byte >= 0xF0 is a supplementary character:
// two UTF-16 units.
int utf16_from_column(const std::string& line, int column) {
    int units = 0, current = 1;
    for (size_t i = 0; i < line.size() && current < column; ++i) {
        const auto c = static_cast<unsigned char>(line[i]);
        if ((c & 0xc0) == 0x80) continue;
        units += c >= 0xf0 ? 2 : 1;
        ++current;
    }
    return units;
}

int column_from_utf16(const std::string& line, int utf16) {
    int units = 0, column = 1;
    for (size_t i = 0; i < line.size() && units < utf16; ++i) {
        const auto c = static_cast<unsigned char>(line[i]);
        if ((c & 0xc0) == 0x80) continue;
        units += c >= 0xf0 ? 2 : 1;
        ++column;
    }
    return column;
}

std::unique_ptr<Client> Client::start(const std::vector<std::string>& command, const std::string& root,
                                      std::string& error, std::chrono::milliseconds timeout) {
    if (command.empty()) { error = "no server command"; return nullptr; }
    int toServer[2], fromServer[2];
    if (pipe(toServer) != 0 || pipe(fromServer) != 0) { error = std::strerror(errno); return nullptr; }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, toServer[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fromServer[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    for (int fd : {toServer[0], toServer[1], fromServer[0], fromServer[1]}) posix_spawn_file_actions_addclose(&actions, fd);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char*> argv;
    for (const auto& part : command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(toServer[0]);
    close(fromServer[1]);
    if (spawned != 0) {
        close(toServer[1]);
        close(fromServer[0]);
        error = command[0] + ": " + std::strerror(spawned);
        return nullptr;
    }
    signal(SIGPIPE, SIG_IGN);  // a dead server must fail a write, not kill the app
    std::unique_ptr<Client> client(new Client());
    client->pid_ = pid;
    client->in_ = toServer[1];
    client->out_ = fromServer[0];
    client->alive_ = true;
    client->reader_ = std::thread([raw = client.get()] { raw->read_loop(); });
    auto init = client->request("initialize", {
        {"processId", static_cast<int>(getpid())},
        {"rootUri", uri_from_path(root)},
        {"clientInfo", {{"name", "floatinghotel"}}},
        {"capabilities", {
            {"general", {{"positionEncodings", {"utf-16"}}}},
            {"textDocument", {
                {"synchronization", {{"didSave", false}}},
                {"definition", {{"linkSupport", false}}},
                {"references", nlohmann::json::object()},
                {"hover", {{"contentFormat", {"markdown", "plaintext"}}}},
                {"callHierarchy", nlohmann::json::object()},
            }},
            {"window", {{"workDoneProgress", false}}},
        }},
        {"workspaceFolders", {{{"uri", uri_from_path(root)}, {"name", "root"}}}},
    });
    if (init.result.wait_for(timeout) != std::future_status::ready) { error = command[0] + " did not initialize"; return nullptr; }
    const auto reply = init.result.get();
    if (reply.contains("error")) { error = command[0] + ": " + reply["error"].dump(); return nullptr; }
    client->capabilities_ = reply["result"].value("capabilities", nlohmann::json::object());
    client->notify("initialized", nlohmann::json::object());
    return client;
}

Client::~Client() {
    if (alive_) {
        auto down = request("shutdown", nullptr);
        down.result.wait_for(std::chrono::seconds(2));
        notify("exit", nullptr);
    }
    if (in_ >= 0) close(in_);
    in_ = -1;
    int status = 0;
    for (int i = 0; i < 50 && pid_ > 0 && waitpid(pid_, &status, WNOHANG) == 0; ++i) usleep(40 * 1000);
    if (pid_ > 0 && waitpid(pid_, &status, WNOHANG) == 0) {
        kill(-pid_, SIGKILL);
        waitpid(pid_, &status, 0);
    }
    if (reader_.joinable()) reader_.join();
    if (out_ >= 0) close(out_);
}

void Client::send(const nlohmann::json& message) {
    const auto bytes = json_rpc::frame(message.dump());
    std::lock_guard lock(write_);
    size_t written = 0;
    while (alive_ && written < bytes.size()) {
        const ssize_t n = write(in_, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { alive_ = false; break; }
        written += static_cast<size_t>(n);
    }
}

Client::Pending Client::request(const std::string& method, nlohmann::json params) {
    const int id = next_id_++;
    std::promise<nlohmann::json> promise;
    Pending pending{id, promise.get_future().share()};
    {
        std::lock_guard lock(pending_mutex_);
        if (!alive_) { promise.set_value({{"error", "language server is not running"}}); return pending; }
        pending_.emplace(id, std::move(promise));
    }
    send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
    if (!alive_) fail_all("language server is not running");
    return pending;
}

void Client::cancel(int id) {
    notify("$/cancelRequest", {{"id", id}});
    std::lock_guard lock(pending_mutex_);
    if (auto found = pending_.find(id); found != pending_.end()) {
        found->second.set_value({{"error", "cancelled"}});
        pending_.erase(found);
    }
}

void Client::notify(const std::string& method, nlohmann::json params) {
    nlohmann::json message{{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null()) message["params"] = std::move(params);
    send(message);
}

void Client::fail_all(const std::string& why) {
    std::lock_guard lock(pending_mutex_);
    for (auto& [id, promise] : pending_) promise.set_value({{"error", why}});
    pending_.clear();
}

void Client::read_loop() {
    json_rpc::Decoder decoder;
    char buffer[65536];
    for (;;) {
        const ssize_t n = read(out_, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        decoder.feed({buffer, static_cast<size_t>(n)});
        while (auto body = decoder.next()) {
            auto message = nlohmann::json::parse(*body, nullptr, false);
            if (message.is_discarded()) continue;
            if (message.contains("method") && message.contains("id")) {
                // Requests a server makes of its client: configuration gets one
                // null per item, registrations and progress tokens an empty
                // success, anything else "method not found".
                const auto method = message["method"].get<std::string>();
                nlohmann::json reply{{"jsonrpc", "2.0"}, {"id", message["id"]}};
                if (method == "workspace/configuration")
                    reply["result"] = nlohmann::json::array_t(message["params"].value("items", nlohmann::json::array()).size(), nullptr);
                else if (method == "client/registerCapability" || method == "client/unregisterCapability" ||
                         method == "window/workDoneProgress/create")
                    reply["result"] = nullptr;
                else reply["error"] = {{"code", -32601}, {"message", "not supported"}};
                send(reply);
            } else if (message.contains("id") && message["id"].is_number_integer()) {
                std::lock_guard lock(pending_mutex_);
                auto found = pending_.find(message["id"].get<int>());
                if (found == pending_.end()) continue;
                if (message.contains("error")) found->second.set_value({{"error", message["error"].value("message", "error")}});
                else found->second.set_value({{"result", message.value("result", nlohmann::json())}});
                pending_.erase(found);
            }
        }
        if (!decoder.error().empty()) break;
    }
    alive_ = false;
    fail_all(decoder.error().empty() ? "language server exited" : "language server sent " + decoder.error());
}

}  // namespace lsp
