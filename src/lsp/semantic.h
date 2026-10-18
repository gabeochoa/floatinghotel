#pragma once

// Semantic navigation for working-tree documents: one language server per
// (repository, server), started on the first request. A request runs on a
// worker and is cancelled at the server when its task is dropped (the active
// document changed). Locations come back canonical, repository-relative where
// they can be, and de-duplicated: a server can report one file under two
// spellings (a symlinked path and its target).

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../util/async_task.h"

namespace lsp { class Client; }

namespace semantic {

enum class Kind { Definition, References, Hover, Incoming, Outgoing };

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

private:
    struct State;
    std::shared_ptr<State> state_;
};

std::string describe(Kind kind);

}  // namespace semantic
