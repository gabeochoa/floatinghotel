#pragma once

// Semantic navigation for working-tree documents: one language server per
// (repository, server), started on the first request. A request runs on a
// worker and is cancelled at the server when its task is dropped (the active
// document changed). Locations come back canonical, repository-relative where
// they can be, and de-duplicated: a server can report one file under two
// spellings (a symlinked path and its target).

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../util/async_task.h"

namespace lsp { class Client; }

namespace semantic {

enum class Kind { Definition, References, Hover, Incoming, Outgoing };

// Language-server lifecycle (fsm_todo.md M11): Unstarted (no client
// attempted) -> Starting (ensure_client starting it) -> Ready (client
// alive and idle; ensure_client is the sole transition into Ready) ->
// Busy (one request in flight) -> Ready on settle via ensure_client, or
// Dead when the client is gone, Failed when a start attempt failed.
// Dead/Failed retry through Starting on the next ensure_client call.
enum class Lsp { Unstarted, Starting, Ready, Busy, Dead, Failed };

inline std::string_view lsp_name(Lsp lsp) {
    switch (lsp) {
        case Lsp::Unstarted: return "Unstarted";
        case Lsp::Starting: return "Starting";
        case Lsp::Ready: return "Ready";
        case Lsp::Busy: return "Busy";
        case Lsp::Dead: return "Dead";
        case Lsp::Failed: return "Failed";
    }
    return "Unstarted";
}

struct Location {
    std::string path;    // repository-relative, or absolute when outside it
    bool inside = true;  // false: outside the repository, opened read-only
    int line = 1;        // 1-based
    int column = 1;      // 1-based code points
    std::string text;    // the line, for the results list
    std::string label;   // call hierarchy: the caller or callee name
};

struct Outcome {
    Kind kind = Kind::Definition;
    std::vector<Location> locations;
    std::string hover;
    std::string error;
    std::string server;
    bool cancelled = false;
};

struct Server {
    std::string name;
    std::string language;
    std::vector<std::string> command;
    std::vector<std::string> extensions;
};

// The table from docs/design/language-servers.md, first on PATH wins.
const std::vector<Server>& table();
// The server for `path`, or nullopt; `missing` names the language when a
// server is known for the extension but none is installed.
std::optional<Server> server_for(const std::string& path, std::string* missing = nullptr);

class Session {
public:
    explicit Session(std::string repo);
    ~Session();
    // `text` is the edit buffer when editing, empty to use the file on disk;
    // `line`/`column` are the caret, 1-based code points.
    async_work::Task<Outcome> run(Kind kind, std::string path, std::string text, int line, int column);
    // Inline (shared atomic in the header) so RepoComponent's slot
    // registry can name the phase without linking semantic.cpp into
    // every unit test. Shared so a worker task outliving the Session
    // still writes a live atomic.
    Lsp lsp_state() const { return lsp_->load(); }
    // The document (repository-relative `path`, as passed to run) closed:
    // queue a textDocument/didClose and drop its version so a reopen
    // starts at version 1 again. Inline and queue-only so navigation
    // (and the unit tests that include it) need not link semantic.cpp;
    // flush_closed_documents()/run() send it on the next frame/request,
    // and a Busy request drains it when it settles, so the UI thread
    // never blocks on it.
    void close_document(const std::string& path) {
        std::lock_guard lock(*pendingMutex_);
        pendingCloses_->insert(path);
    }
    void flush_closed_documents();

private:
    struct State;
    std::shared_ptr<State> state_;
    std::shared_ptr<std::atomic<Lsp>> lsp_ = std::make_shared<std::atomic<Lsp>>(Lsp::Unstarted);
    std::shared_ptr<std::mutex> pendingMutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::set<std::string>> pendingCloses_ = std::make_shared<std::set<std::string>>();
};

std::string describe(Kind kind);

}  // namespace semantic
