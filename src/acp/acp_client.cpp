#include "acp_client.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace acp {
namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string first_nonempty(const nlohmann::json& value, const char* key) {
    return value.is_object() ? value.value(key, std::string{}) : std::string{};
}

}  // namespace

std::string find_on_path(const std::string& program) {
    if (program.empty()) return "";
    if (program.find('/') != std::string::npos)
        return access(program.c_str(), X_OK) == 0 ? program : "";
    std::stringstream paths(env("PATH"));
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        if (dir.empty()) continue;
        const std::string candidate = dir + "/" + program;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return "";
}

bool on_path(const std::string& program) { return !find_on_path(program).empty(); }

std::vector<std::string> split_command(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    char quote = 0;
    bool quoted = false;
    auto flush = [&]() {
        if (!current.empty() || quoted) parts.push_back(current);
        current.clear();
        quoted = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < text.size()) current += text[++i];
            else current += c;
        } else if (c == '\'' || c == '"') {
            quote = c;
            quoted = true;
        } else if (c == '\\' && i + 1 < text.size()) current += text[++i];
        else if (c == ' ' || c == '\t' || c == '\n') flush();
        else current += c;
    }
    flush();
    return parts;
}

std::vector<AgentDef> default_agents() {
    std::vector<AgentDef> agents;
    auto add = [&](std::string id, std::string name, std::string description,
                   std::vector<std::string> command, const char* envName,
                   std::vector<std::pair<std::string, std::string>> extraEnv = {}) {
        if (const std::string override_ = env(envName); !override_.empty())
            command = split_command(override_);
        const bool available = !command.empty() && on_path(command.front());
        agents.push_back({std::move(id), std::move(name), std::move(description),
                          std::move(command), available, std::move(extraEnv)});
    };

    add("muse", "Muse", "Runs `muse acp` (native ACP).",
        {"muse", "acp"}, "FH_ACP_MUSE_CMD");

    // The adapters bundle their own agent binaries, which miss the auth /
    // proxy setup of the installed CLIs (at Meta their API calls hang);
    // point them at the installed binary unless the user already did.
    std::vector<std::pair<std::string, std::string>> claudeEnv;
    if (env("CLAUDE_CODE_EXECUTABLE").empty())
        if (const std::string exe = find_on_path("claude"); !exe.empty())
            claudeEnv.push_back({"CLAUDE_CODE_EXECUTABLE", exe});
    std::vector<std::string> claude;
    if (on_path("claude-agent-acp")) claude = {"claude-agent-acp"};
    else if (on_path("claude-code-acp")) claude = {"claude-code-acp"};
    else if (on_path("npx")) claude = {"npx", "-y", "@agentclientprotocol/claude-agent-acp"};
    else claude = {"claude-agent-acp"};
    add("claude", "Claude", "Runs the Claude ACP adapter (claude-agent-acp, or via npx), on the installed `claude` binary.",
        claude, "FH_ACP_CLAUDE_CMD", std::move(claudeEnv));

    std::vector<std::pair<std::string, std::string>> codexEnv;
    if (env("CODEX_PATH").empty())
        if (const std::string exe = find_on_path("codex"); !exe.empty())
            codexEnv.push_back({"CODEX_PATH", exe});
    std::vector<std::string> codex;
    if (on_path("codex-acp")) codex = {"codex-acp"};
    else if (on_path("npx")) codex = {"npx", "-y", "@agentclientprotocol/codex-acp"};
    else codex = {"codex-acp"};
    add("codex", "Codex", "Runs the Codex ACP adapter (codex-acp, or via npx), on the installed `codex` binary.",
        codex, "FH_ACP_CODEX_CMD", std::move(codexEnv));

    return agents;
}

const AgentDef* find_agent(const std::vector<AgentDef>& agents, const std::string& id) {
    for (const auto& agent : agents)
        if (agent.id == id) return &agent;
    return nullptr;
}

// ---------------------------------------------------------------- transcript

void Transcript::chunk(EntryKind kind, const std::string& text) {
    if (entries.empty() || entries.back().kind != kind || !entries.back().toolCallId.empty()) {
        entries.push_back({kind, "", "", "", ""});
        // Long sessions stay bounded: the oldest turns scroll away.
        if (entries.size() > 800) entries.erase(entries.begin(), entries.begin() + 100);
    }
    entries.back().text += text;
}

void Transcript::user(const std::string& text) {
    entries.push_back({EntryKind::User, text, "", "", ""});
}

void Transcript::system(const std::string& text) {
    entries.push_back({EntryKind::System, text, "", "", ""});
}

void Transcript::error(const std::string& text) {
    entries.push_back({EntryKind::Error, text, "", "", ""});
}

Entry* Transcript::tool_call(const std::string& id) {
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (it->kind == EntryKind::ToolCall && it->toolCallId == id) return &*it;
    entries.push_back({EntryKind::ToolCall, "", "", "", id});
    return &entries.back();
}

void Transcript::describe_tool_content(Entry& entry, const nlohmann::json& update) {
    // A tool_call_update's content is the tool call's *current* content,
    // not a delta (agents resend it whole), so an update that carries
    // content or locations replaces the description instead of appending.
    if (!update.contains("content") && !update.contains("locations")) return;
    // Diffs and locations are the "agent is editing my code" evidence; the
    // full before/after text stays out of the transcript (the editor and
    // diff view show it, and whole files would flood the sidebar).
    std::string text;
    const auto content = update.value("content", nlohmann::json::array());
    if (content.is_array())
        for (const auto& item : content) {
            const std::string type = item.value("type", "");
            if (type == "diff") {
                const std::string path = item.value("path", "");
                if (!text.empty()) text += "\n";
                text += "± " + path;
                const std::string fresh = item.value("newText", "");
                if (!fresh.empty()) {
                    size_t lines = 1;
                    for (char c : fresh) lines += c == '\n' ? 1 : 0;
                    text += " (" + std::to_string(lines) + (lines == 1 ? " line)" : " lines)");
                }
            } else if (type == "content") {
                const std::string body = first_nonempty(item.value("content", nlohmann::json::object()), "text");
                if (!body.empty()) {
                    if (!text.empty()) text += "\n";
                    text += body.substr(0, 2000);
                }
            }
        }
    const auto locations = update.value("locations", nlohmann::json::array());
    if (locations.is_array())
        for (const auto& location : locations) {
            const std::string path = location.value("path", "");
            if (path.empty() || text.find(path) != std::string::npos) continue;
            if (!text.empty()) text += "\n";
            text += path;
            if (const int line = location.value("line", 0); line > 0) text += ":" + std::to_string(line);
        }
    if (!text.empty() || update.contains("content")) entry.text = std::move(text);
}

void Transcript::apply_update(const nlohmann::json& update) {
    const std::string kind = update.value("sessionUpdate", "");
    const nlohmann::json content = update.value("content", nlohmann::json::object());
    const std::string text = content.is_object() && content.value("type", "") == "text"
        ? content.value("text", "") : "";
    if (kind == "agent_message_chunk") chunk(EntryKind::Agent, text);
    else if (kind == "agent_thought_chunk") chunk(EntryKind::Thought, text);
    else if (kind == "user_message_chunk") chunk(EntryKind::User, text);
    else if (kind == "tool_call" || kind == "tool_call_update") {
        Entry& entry = *tool_call(update.value("toolCallId", ""));
        if (const std::string title = update.value("title", ""); !title.empty()) entry.title = title;
        if (const std::string status = update.value("status", ""); !status.empty()) entry.status = status;
        if (entry.title.empty()) entry.title = update.value("kind", "tool");
        describe_tool_content(entry, update);
    } else if (kind == "plan") {
        std::string plan;
        const auto items = update.value("entries", nlohmann::json::array());
        if (items.is_array())
            for (const auto& item : items) {
                const std::string status = item.value("status", "");
                plan += status == "completed" ? "[x] " : status == "in_progress" ? "[>] " : "[ ] ";
                plan += item.value("content", "");
                plan += "\n";
            }
        if (!entries.empty() && entries.back().kind == EntryKind::Plan) entries.back().text = plan;
        else entries.push_back({EntryKind::Plan, plan, "", "", ""});
    }
    // available_commands_update / mode / config / info / usage updates are
    // Client state; anything unknown is ignored (forward compatibility).
}

std::string Transcript::plain() const {
    std::string out;
    for (const auto& entry : entries) {
        switch (entry.kind) {
            case EntryKind::User: out += "You: "; break;
            case EntryKind::Agent: break;
            case EntryKind::Thought: out += "(thinking) "; break;
            case EntryKind::ToolCall: out += "[" + entry.title + (entry.status.empty() ? "" : " · " + entry.status) + "] "; break;
            case EntryKind::Plan: out += "Plan:\n"; break;
            case EntryKind::System: out += "— "; break;
            case EntryKind::Error: out += "! "; break;
        }
        out += entry.text;
        if (out.empty() || out.back() != '\n') out += "\n";
    }
    return out;
}

// ------------------------------------------------------------------------ fs

FsRoot FsRoot::make(std::filesystem::path root) {
    std::error_code ec;
    const auto absolute = root.is_absolute() ? root : std::filesystem::current_path(ec) / root;
    // canonical() (not weakly_) so a /tmp root and a /private/tmp target
    // compare equal; resolve() canonicalises targets the same way.
    auto canonical = std::filesystem::canonical(absolute, ec);
    if (ec) canonical = std::filesystem::weakly_canonical(absolute, ec);
    return {ec ? root : canonical};
}

std::optional<std::filesystem::path> FsRoot::resolve(const std::string& path) const {
    if (path.empty()) return std::nullopt;
    std::error_code ec;
    std::filesystem::path raw(path);
    std::filesystem::path absolute = raw.is_absolute() ? raw : root / raw;
    // Resolve the existing prefix so a symlink inside the repo that points
    // out of it cannot smuggle a write past the lexical check; the rest
    // (a file being created) is checked lexically.
    std::filesystem::path existing = absolute;
    std::filesystem::path tail;
    while (!std::filesystem::exists(existing, ec) && existing.has_parent_path()) {
        tail = tail.empty() ? existing.filename() : existing.filename() / tail;
        existing = existing.parent_path();
    }
    const auto resolved = std::filesystem::weakly_canonical(
        tail.empty() ? std::filesystem::canonical(existing, ec)
                     : std::filesystem::canonical(existing, ec) / tail, ec);
    if (ec) return std::nullopt;
    const std::string rootText = root.string();
    const std::string text = resolved.string();
    if (text != rootText && text.rfind(rootText + "/", 0) != 0) return std::nullopt;
    return resolved;
}

std::optional<std::string> read_text_file(const FsRoot& root, const std::string& path,
                                          int line, int limit, std::string& error) {
    const auto target = root.resolve(path);
    if (!target) { error = "path is outside the repository: " + path; return std::nullopt; }
    std::ifstream in(*target, std::ios::binary);
    if (!in) { error = "cannot read " + path; return std::nullopt; }
    if (line <= 1 && limit <= 0) {
        std::ostringstream all;
        all << in.rdbuf();
        if (in.bad()) { error = "cannot read " + path; return std::nullopt; }
        return all.str();
    }
    std::string out, current;
    int number = 1, taken = 0;
    while (std::getline(in, current)) {
        if (number++ < line) continue;
        out += current;
        out += '\n';
        if (limit > 0 && ++taken >= limit) break;
    }
    return out;
}

bool write_text_file(const FsRoot& root, const std::string& path, const std::string& content,
                     std::string& error) {
    const auto target = root.resolve(path);
    if (!target) { error = "path is outside the repository: " + path; return false; }
    std::error_code ec;
    if (target->has_parent_path()) std::filesystem::create_directories(target->parent_path(), ec);
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + path; return false; }
    out << content;
    out.flush();
    if (!out) { error = "cannot write " + path; return false; }
    return true;
}

// -------------------------------------------------------------------- client

std::unique_ptr<Client> Client::spawn(const AgentDef& agent, const std::string& cwd, std::string& error,
                                      std::chrono::milliseconds initialize_timeout) {
    if (agent.command.empty()) { error = "no agent command"; return nullptr; }
    int toAgent[2], fromAgent[2], errAgent[2];
    if (pipe(toAgent) != 0 || pipe(fromAgent) != 0 || pipe(errAgent) != 0) {
        error = std::strerror(errno);
        return nullptr;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    posix_spawn_file_actions_adddup2(&actions, toAgent[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fromAgent[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errAgent[1], STDERR_FILENO);
    for (int fd : {toAgent[0], toAgent[1], fromAgent[0], fromAgent[1], errAgent[0], errAgent[1]})
        posix_spawn_file_actions_addclose(&actions, fd);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char*> argv;
    for (const auto& part : agent.command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    // environ overlaid with the agent's entries (same key replaced).
    std::vector<std::string> envStrings;
    for (char** entry = environ; *entry; ++entry) {
        std::string_view text(*entry);
        bool replaced = false;
        for (const auto& pair : agent.env)
            if (text.size() > pair.first.size() && text.substr(0, pair.first.size()) == pair.first &&
                text[pair.first.size()] == '=')
                replaced = true;
        if (!replaced) envStrings.emplace_back(*entry);
    }
    for (const auto& [key, value] : agent.env) envStrings.push_back(key + "=" + value);
    std::vector<char*> envp;
    for (auto& entry : envStrings) envp.push_back(entry.data());
    envp.push_back(nullptr);
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(toAgent[0]);
    close(fromAgent[1]);
    close(errAgent[1]);
    if (spawned != 0) {
        close(toAgent[1]);
        close(fromAgent[0]);
        close(errAgent[0]);
        error = agent.command.front() + ": " + std::strerror(spawned);
        return nullptr;
    }
    signal(SIGPIPE, SIG_IGN);  // a dead agent must fail a write, not kill the app
    std::unique_ptr<Client> client(new Client());
    client->agent_ = agent;
    client->cwd_ = cwd;
    client->initializeTimeout_ = initialize_timeout;
    client->pid_ = pid;
    client->in_ = toAgent[1];
    client->out_ = fromAgent[0];
    client->err_ = errAgent[0];
    client->alive_ = true;
    client->phase_ = Phase::Starting;
    client->deadline_ = std::chrono::steady_clock::now() + initialize_timeout;
    client->reader_ = std::thread([raw = client.get()] { raw->read_loop(raw->out_, true); });
    client->errReader_ = std::thread([raw = client.get()] { raw->read_loop(raw->err_, false); });
    const int id = client->request("initialize", {
        {"protocolVersion", 1},
        {"clientInfo", {{"name", "floatinghotel"}, {"title", "Floating Hotel"}, {"version", "0.1"}}},
        {"clientCapabilities", {
            {"fs", {{"readTextFile", true}, {"writeTextFile", true}}},
            {"terminal", false},
        }},
    });
    client->pending_[id] = Purpose::Initialize;
    return client;
}

Client::~Client() {
    if (in_ >= 0) close(in_);  // EOF: well-behaved agents exit on stdin close
    in_ = -1;
    int status = 0;
    for (int i = 0; i < 25 && pid_ > 0 && waitpid(pid_, &status, WNOHANG) == 0; ++i) usleep(40 * 1000);
    if (pid_ > 0 && waitpid(pid_, &status, WNOHANG) == 0) {
        kill(-pid_, SIGKILL);
        waitpid(pid_, &status, 0);
    }
    alive_ = false;
    if (out_ >= 0) close(out_);
    if (err_ >= 0) close(err_);
    if (reader_.joinable()) reader_.join();
    if (errReader_.joinable()) errReader_.join();
}

void Client::send(const nlohmann::json& message) {
    const std::string bytes = message.dump() + "\n";
    std::lock_guard lock(write_);
    size_t written = 0;
    while (alive_ && written < bytes.size()) {
        const ssize_t n = write(in_, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { alive_ = false; break; }
        written += static_cast<size_t>(n);
    }
}

int Client::request(const std::string& method, nlohmann::json params) {
    const int id = nextId_++;
    send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
    return id;
}

void Client::notify(const std::string& method, nlohmann::json params) {
    send({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

bool Client::prompt(std::vector<nlohmann::json> blocks) {
    if (phase_ != Phase::Ready || sessionId_.empty()) return false;
    nlohmann::json prompt = nlohmann::json::array();
    for (auto& block : blocks) prompt.push_back(std::move(block));
    phase_ = Phase::Prompting;
    pending_[request("session/prompt", {{"sessionId", sessionId_}, {"prompt", std::move(prompt)}})] = Purpose::Prompt;
    return true;
}

void Client::cancel() {
    if (!sessionId_.empty()) notify("session/cancel", {{"sessionId", sessionId_}});
}

bool Client::new_session() {
    if (phase_ != Phase::Ready && phase_ != Phase::Prompting) return false;
    phase_ = Phase::Starting;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    sessionId_.clear();
    pending_[request("session/new", {{"cwd", cwd_}, {"mcpServers", nlohmann::json::array()}})] = Purpose::NewSession;
    return true;
}

void Client::set_mode(const std::string& modeId) {
    if (sessionId_.empty()) return;
    pending_[request("session/set_mode", {{"sessionId", sessionId_}, {"modeId", modeId}})] = Purpose::SetMode;
}

void Client::set_config_option(const std::string& configId, const std::string& value) {
    if (sessionId_.empty()) return;
    pending_[request("session/set_config_option", {{"sessionId", sessionId_}, {"configId", configId}, {"value", value}})] =
        Purpose::SetConfig;
}

void Client::authenticate(const std::string& methodId) {
    pending_[request("authenticate", {{"methodId", methodId}})] = Purpose::Authenticate;
}

void Client::answer_permission(const nlohmann::json& id, const std::string& optionId) {
    nlohmann::json outcome = optionId.empty()
        ? nlohmann::json{{"outcome", "cancelled"}}
        : nlohmann::json{{"outcome", "selected"}, {"optionId", optionId}};
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"outcome", std::move(outcome)}}}});
}

void Client::answer_fs_read(const nlohmann::json& id, const std::string& content) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"content", content}}}});
}

void Client::answer_fs_write(const nlohmann::json& id) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
}

void Client::answer_error(const nlohmann::json& id, const std::string& message) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32603}, {"message", message}}}});
}

void Client::read_loop(int fd, bool is_stdout) {
    LineDecoder decoder;
    char buffer[65536];
    for (;;) {
        const ssize_t n = read(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        decoder.feed({buffer, static_cast<size_t>(n)});
        while (auto line = decoder.next()) {
            std::lock_guard lock(queue_mutex_);
            (is_stdout ? lines_ : errLines_).push(std::move(*line));
        }
        if (!decoder.error().empty()) break;
    }
    if (is_stdout) {
        std::lock_guard lock(queue_mutex_);
        lines_.push("");  // EOF sentinel
    }
}

void Client::pump() {
    std::vector<std::string> lines, errLines;
    {
        std::lock_guard lock(queue_mutex_);
        while (!lines_.empty()) { lines.push_back(lines_.front()); lines_.pop(); }
        while (!errLines_.empty()) { errLines.push_back(errLines_.front()); errLines_.pop(); }
    }
    for (const auto& line : errLines) {
        if (line.empty()) continue;
        lastStderr_ = line;
        if (on_event) on_event({Event::Kind::Log, {}, {}, {}, line, false});
    }
    for (const auto& line : lines) {
        if (exited_) break;
        if (line.empty()) {  // stdout EOF, or a stray blank line before it
            if (!alive_ || &line == &lines.back()) { fail("agent exited"); break; }
            continue;
        }
        // Launchers sometimes banner on stdout; a non-JSON line is log
        // noise, not a protocol death.
        if (line.front() != '{') {
            if (on_event) on_event({Event::Kind::Log, {}, {}, {}, line, false});
            continue;
        }
        const auto message = nlohmann::json::parse(line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) continue;
        handle(message);
    }
    if (!exited_ && phase_ == Phase::Starting && std::chrono::steady_clock::now() > deadline_)
        fail(agent_.name + " did not start in time");
}

void Client::handle(const nlohmann::json& message) {
    const bool hasMethod = message.contains("method");
    const bool hasId = message.contains("id") && !message["id"].is_null();
    if (hasMethod && hasId) {
        // Agent -> client request. fs and permission need editor state, so
        // they become events; anything else (terminal/* included -- the
        // capability is advertised false) is method-not-found.
        const std::string method = message.value("method", "");
        Event event;
        event.id = message["id"];
        event.params = message.value("params", nlohmann::json::object());
        if (method == "session/request_permission") event.kind = Event::Kind::Permission;
        else if (method == "fs/read_text_file") event.kind = Event::Kind::FsRead;
        else if (method == "fs/write_text_file") event.kind = Event::Kind::FsWrite;
        else {
            send({{"jsonrpc", "2.0"}, {"id", message["id"]},
                  {"error", {{"code", -32601}, {"message", "not supported: " + method}}}});
            return;
        }
        if (on_event) on_event(event);
        else answer_error(event.id, "no handler");
    } else if (hasMethod) {
        if (message.value("method", "") != "session/update") return;
        const auto params = message.value("params", nlohmann::json::object());
        const auto update = params.value("update", nlohmann::json::object());
        const std::string kind = update.value("sessionUpdate", "");
        if (kind == "current_mode_update") currentMode_ = update.value("currentModeId", currentMode_);
        else if (kind == "config_option_update") apply_config_options(update.value("configOptions", nlohmann::json::array()));
        else if (kind == "session_info_update") {
            if (auto title = update.value("title", ""); !title.empty()) sessionTitle_ = title;
        } else if (kind == "available_commands_update") {
            availableCommands_.clear();
            const auto commands = update.value("availableCommands", nlohmann::json::array());
            if (commands.is_array())
                for (const auto& command : commands) availableCommands_.push_back(command.value("name", ""));
        }
        if (on_event) on_event({Event::Kind::Update, update, {}, {}, "", false});
    } else if (hasId) handle_response(message);
}

void Client::handle_response(const nlohmann::json& message) {
    if (!message["id"].is_number_integer()) return;
    const int id = message["id"].get<int>();
    const auto found = pending_.find(id);
    if (found == pending_.end()) return;
    const Purpose purpose = found->second;
    pending_.erase(found);
    const bool failed = message.contains("error");
    const nlohmann::json result = message.value("result", nlohmann::json::object());
    const std::string errorText = failed ? message["error"].value("message", "error") : "";
    const bool authError = failed && message["error"].value("code", 0) == -32000;
    if (authError) authRequired_ = true;

    switch (purpose) {
        case Purpose::Initialize:
            if (failed) { fail(agent_.name + ": " + errorText); return; }
            embeddedContext_ = result.value("agentCapabilities", nlohmann::json::object())
                                   .value("promptCapabilities", nlohmann::json::object())
                                   .value("embeddedContext", false);
            agentName_ = result.value("agentInfo", nlohmann::json::object()).value("title",
                        result.value("agentInfo", nlohmann::json::object()).value("name", agent_.name));
            authMethods_.clear();
            if (const auto methods = result.value("authMethods", nlohmann::json::array()); methods.is_array())
                for (const auto& method : methods)
                    authMethods_.push_back({method.value("id", ""), method.value("name", ""), method.value("description", "")});
            deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            pending_[request("session/new", {{"cwd", cwd_}, {"mcpServers", nlohmann::json::array()}})] = Purpose::NewSession;
            break;
        case Purpose::NewSession:
            if (failed) {
                if (authError || !authMethods_.empty()) {
                    authRequired_ = true;
                    phase_ = Phase::Ready;  // connected; signing in unlocks sessions
                    if (on_event) on_event({Event::Kind::Log, {}, {}, {}, "Sign in required: " + errorText, false});
                } else fail(agent_.name + ": " + errorText);
                return;
            }
            apply_session_result(result);
            phase_ = Phase::Ready;
            break;
        case Purpose::Prompt:
            if (failed) { finish_prompt(errorText, true); return; }
            finish_prompt(result.value("stopReason", "end_turn"), false);
            break;
        case Purpose::Authenticate:
            if (failed) {
                if (on_event) on_event({Event::Kind::Log, {}, {}, {}, "Sign in failed: " + errorText, false});
                return;
            }
            authRequired_ = false;
            if (sessionId_.empty()) new_session();
            break;
        case Purpose::SetMode:
        case Purpose::SetConfig:
            if (!failed && result.contains("configOptions")) apply_config_options(result["configOptions"]);
            if (failed && on_event) on_event({Event::Kind::Log, {}, {}, {}, errorText, false});
            break;
    }
}

void Client::apply_session_result(const nlohmann::json& result) {
    sessionId_ = result.value("sessionId", "");
    sessionTitle_.clear();
    modes_.clear();
    const auto modes = result.value("modes", nlohmann::json::object());
    currentMode_ = modes.value("currentModeId", "");
    if (const auto available = modes.value("availableModes", nlohmann::json::array()); available.is_array())
        for (const auto& mode : available)
            modes_.push_back({mode.value("id", ""), mode.value("name", ""), mode.value("description", "")});
    apply_config_options(result.value("configOptions", nlohmann::json::array()));
}

void Client::apply_config_options(const nlohmann::json& list) {
    if (!list.is_array()) return;
    configOptions_.clear();
    for (const auto& option : list) {
        if (option.value("type", "") != "select") continue;
        ConfigOption parsed{option.value("id", ""), option.value("name", ""), option.value("currentValue", ""), {}};
        const auto options = option.value("options", nlohmann::json::array());
        if (options.is_array())
            for (const auto& choice : options) {
                // Options are flat {value,name} or grouped {group,options:[...]}.
                if (choice.contains("options") && choice["options"].is_array())
                    for (const auto& inner : choice["options"])
                        parsed.options.push_back({inner.value("value", ""), inner.value("name", "")});
                else parsed.options.push_back({choice.value("value", ""), choice.value("name", "")});
            }
        configOptions_.push_back(std::move(parsed));
    }
}

void Client::finish_prompt(std::string stopReason, bool failed) {
    if (phase_ == Phase::Prompting) phase_ = Phase::Ready;
    if (on_event) on_event({Event::Kind::PromptDone, {}, {}, {}, std::move(stopReason), failed});
}

void Client::fail(std::string why) {
    if (exited_) return;
    exited_ = true;
    alive_ = false;
    const bool cleanExit = why == "agent exited";
    if (cleanExit && !lastStderr_.empty() && phase_ != Phase::Prompting && phase_ != Phase::Ready)
        why += ": " + lastStderr_;
    error_ = why;
    if (phase_ == Phase::Prompting) finish_prompt(why, true);
    phase_ = phase_ == Phase::Starting ? Phase::Failed : Phase::Dead;
    if (on_event) on_event({Event::Kind::Exited, {}, {}, {}, why, true});
}

}  // namespace acp
