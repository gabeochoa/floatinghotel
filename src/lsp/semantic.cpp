#include "semantic.h"

#include "lsp_client.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace semantic {

namespace fs = std::filesystem;

const std::vector<Server>& table() {
    static const std::vector<Server> servers{
        {"clangd", "C and C++", {"clangd", "--background-index"}, {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".m", ".mm"}},
        {"sourcekit-lsp", "Swift", {"sourcekit-lsp"}, {".swift"}},
        {"pyright", "Python", {"pyright-langserver", "--stdio"}, {".py", ".pyi"}},
        {"pylsp", "Python", {"pylsp"}, {".py", ".pyi"}},
        {"typescript-language-server", "TypeScript and JavaScript", {"typescript-language-server", "--stdio"},
         {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}},
        {"gopls", "Go", {"gopls"}, {".go"}},
        {"rust-analyzer", "Rust", {"rust-analyzer"}, {".rs"}},
    };
    return servers;
}

static bool on_path(const std::string& program) {
    if (program.find('/') != std::string::npos) return access(program.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    std::stringstream dirs(path ? path : "/usr/bin:/bin:/usr/local/bin:/opt/homebrew/bin");
    for (std::string dir; std::getline(dirs, dir, ':');)
        if (!dir.empty() && access((fs::path(dir) / program).c_str(), X_OK) == 0) return true;
    return false;
}

std::optional<Server> server_for(const std::string& path, std::string* missing) {
    auto extension = fs::path(path).extension().string();
    for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string language;
    for (const auto& server : table()) {
        if (std::find(server.extensions.begin(), server.extensions.end(), extension) == server.extensions.end()) continue;
        if (on_path(server.command.front())) return server;
        if (language.empty()) language = server.language + " navigation needs " + server.name;
    }
    if (missing) *missing = language;
    return std::nullopt;
}

std::string describe(Kind kind) {
    switch (kind) {
        case Kind::Definition: return "definition";
        case Kind::References: return "references";
        case Kind::Hover: return "type";
        case Kind::Incoming: return "callers";
        case Kind::Outgoing: return "callees";
        default: return "";
    }
}

struct Session::State {
    std::string repo, canonicalRepo;
    std::mutex mutex;  // one request at a time per repository: servers answer in order anyway
    std::map<std::string, std::unique_ptr<lsp::Client>> clients;
    std::map<std::string, std::pair<int, std::string>> documents;  // uri -> version, text
};

Session::Session(std::string repo) : state_(std::make_shared<State>()) {
    state_->repo = std::move(repo);
    std::error_code ec;
    state_->canonicalRepo = fs::canonical(state_->repo, ec).string();
    if (ec) state_->canonicalRepo = state_->repo;
}

Session::~Session() = default;

static std::string read_line(const fs::path& file, int line) {
    std::ifstream in(file, std::ios::binary);
    std::string text;
    for (int i = 1; i <= line && std::getline(in, text); ++i) {}
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

// Hover text for a one-notice display: the declaration (the fenced code
// block servers put last) first, then the description, at most six lines.
static std::string strip_markdown(const std::string& value) {
    std::stringstream in(value);
    std::vector<std::string> code, prose;
    bool fenced = false;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("```", 0) == 0) { fenced = !fenced; continue; }
        if (line.find_first_not_of(" \t-_*") == std::string::npos) continue;
        if (fenced && line.rfind("//", line.find_first_not_of(" \t")) == line.find_first_not_of(" \t")) continue;
        (fenced ? code : prose).push_back(line);
    }
    std::string out;
    int kept = 0;
    for (const auto* lines : {&code, &prose})
        for (const auto& line : *lines)
            if (kept++ < 6) out += (out.empty() ? "" : "\n") + line;
    return out;
}

async_work::Task<Outcome> Session::run(Kind kind, std::string path, std::string text, int line, int column) {
    return async_work::launch([state = state_, kind, path = std::move(path), text = std::move(text), line, column](std::stop_token stop) mutable {
        Outcome out;
        if (text.empty()) {  // not editing: the server reads what is on disk
            std::ifstream in(fs::path(state->canonicalRepo) / path, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), {});
        }
        out.kind = kind;
        std::string missing;
        const auto server = server_for(path, &missing);
        if (!server) { out.error = missing.empty() ? "No language server handles this file type" : missing + " (not found on PATH)"; return out; }
        out.server = server->name;
        std::lock_guard lock(state->mutex);
        auto& client = state->clients[server->name];
        if (client && !client->alive()) client.reset();
        if (!client) {
            std::string error;
            client = lsp::Client::start(server->command, state->canonicalRepo, error);
            if (!client) { out.error = "Could not start " + server->name + ": " + error; return out; }
        }
        const auto file = fs::path(state->canonicalRepo) / path;
        const auto uri = lsp::uri_from_path(file.string());
        auto& document = state->documents[uri];
        if (document.first == 0) {
            document = {1, text};
            client->notify("textDocument/didOpen", {{"textDocument", {{"uri", uri}, {"languageId", fs::path(path).extension().string().substr(1)},
                                                                     {"version", 1}, {"text", text}}}});
        } else if (document.second != text) {
            document = {document.first + 1, text};
            client->notify("textDocument/didChange", {{"textDocument", {{"uri", uri}, {"version", document.first}}},
                                                       {"contentChanges", {{{"text", text}}}}});
        }
        std::string caretLine;
        { std::stringstream lines(text); for (int i = 1; i <= line && std::getline(lines, caretLine); ++i) {} }
        nlohmann::json at{{"textDocument", {{"uri", uri}}},
                          {"position", {{"line", line - 1}, {"character", lsp::utf16_from_column(caretLine, column)}}}};
        // Wait, but give up at the server the moment the caller drops the task.
        auto ask = [&](const std::string& method, nlohmann::json params) -> std::optional<nlohmann::json> {
            auto pending = client->request(method, std::move(params));
            while (pending.result.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
                if (stop.stop_requested()) { client->cancel(pending.id); out.error = "cancelled"; return std::nullopt; }
            }
            auto reply = pending.result.get();
            if (reply.contains("error")) { out.error = server->name + ": " + reply["error"].get<std::string>(); return std::nullopt; }
            return reply["result"];
        };
        std::set<std::tuple<std::string, int, int>> seen;
        auto add = [&](const std::string& targetUri, const nlohmann::json& range, std::string label = {}) {
            std::error_code ec;
            auto target = fs::path(lsp::path_from_uri(targetUri));
            const auto canonical = fs::weakly_canonical(target, ec);
            if (!ec) target = canonical;
            const auto relative = target.lexically_relative(state->canonicalRepo);
            Location location;
            location.inside = !relative.empty() && *relative.begin() != "..";
            location.path = location.inside ? relative.string() : target.string();
            location.line = range["start"].value("line", 0) + 1;
            location.text = read_line(target, location.line);
            location.column = lsp::column_from_utf16(location.text, range["start"].value("character", 0));
            location.label = std::move(label);
            if (seen.insert({location.path, location.line, location.column}).second) out.locations.push_back(std::move(location));
        };
        auto addAll = [&](const nlohmann::json& result) {
            if (result.is_object()) add(result.value("uri", result.value("targetUri", "")), result.contains("range") ? result["range"] : result["targetSelectionRange"]);
            else if (result.is_array())
                for (const auto& item : result) add(item.value("uri", item.value("targetUri", "")), item.contains("range") ? item["range"] : item["targetSelectionRange"]);
        };
        switch (kind) {
            case Kind::Definition: if (auto r = ask("textDocument/definition", at)) addAll(*r); break;
            case Kind::References: {
                auto params = at;
                params["context"] = {{"includeDeclaration", true}};
                if (auto r = ask("textDocument/references", params)) addAll(*r);
                break;
            }
            case Kind::Hover:
                if (auto r = ask("textDocument/hover", at); r && r->is_object()) {
                    const auto& contents = (*r)["contents"];
                    std::string value = contents.is_string() ? contents.get<std::string>()
                        : contents.is_object() ? contents.value("value", "")
                        : contents.is_array() && !contents.empty() ? (contents[0].is_string() ? contents[0].get<std::string>() : contents[0].value("value", "")) : "";
                    out.hover = strip_markdown(value);
                }
                break;
            case Kind::Incoming:
            case Kind::Outgoing: {
                auto items = ask("textDocument/prepareCallHierarchy", at);
                if (!items || !items->is_array() || items->empty()) break;
                const bool incoming = kind == Kind::Incoming;
                auto calls = ask(incoming ? "callHierarchy/incomingCalls" : "callHierarchy/outgoingCalls", {{"item", (*items)[0]}});
                if (!calls || !calls->is_array()) break;
                for (const auto& call : *calls) {
                    const auto& other = call[incoming ? "from" : "to"];
                    // Callers: where in the caller the call happens. Callees: the callee itself.
                    const auto& range = incoming && call.contains("fromRanges") && !call["fromRanges"].empty() ? call["fromRanges"][0] : other["selectionRange"];
                    add(other.value("uri", ""), range, other.value("name", ""));
                }
                break;
            }
            default: break;
        }
        return out;
    }, async_work::Priority::Foreground, Outcome{.error = "Background queue is full; retry"});
}

}  // namespace semantic
