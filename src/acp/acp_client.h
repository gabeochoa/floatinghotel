#pragma once

// Agent Client Protocol (ACP v1, agentclientprotocol.com) client: an agent
// process (Muse, Claude Code, Codex, ...) is spawned over stdio and spoken
// to in newline-delimited JSON-RPC 2.0. The editor is the client; the agent
// asks it to read and write files (fs/read_text_file, fs/write_text_file)
// and to approve tool calls (session/request_permission), which is how an
// agent in the sidebar edits code live in the editor.
//
// Threading mirrors src/lsp/lsp_client.h but with one difference: agent ->
// client requests need editor state, so the reader thread only enqueues
// raw messages. Every public method (spawn included) and every Event
// callback runs on the single owner thread that calls pump() once a frame.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <sys/types.h>

namespace acp {

// One launchable agent. `command` is the resolved argv; `available` is a
// PATH probe only (spawn can still fail), used to disable picker rows.
// `env` overlays the inherited environment at spawn (the ACP adapters for
// Claude/Codex are pointed at the installed `claude`/`codex` binaries this
// way, so they inherit that binary's auth and network setup).
struct AgentDef {
    std::string id;           // "muse" | "claude" | "codex"
    std::string name;         // "Muse", "Claude", "Codex"
    std::string description;  // how it is launched, for the empty state
    std::vector<std::string> command;
    bool available = false;
    std::vector<std::pair<std::string, std::string>> env;
};

// The built-in registry: Muse serves ACP natively (`muse acp`); Claude and
// Codex go through their ACP adapters when installed and fall back to
// `npx -y @agentclientprotocol/...`. FH_ACP_<ID>_CMD replaces the command
// for one agent (whitespace-split, single/double quotes honoured).
std::vector<AgentDef> default_agents();
const AgentDef* find_agent(const std::vector<AgentDef>& agents, const std::string& id);
std::vector<std::string> split_command(const std::string& text);
bool on_path(const std::string& program);
// Absolute path of `program` as PATH resolution finds it ("" when absent).
std::string find_on_path(const std::string& program);

// Newline-delimited JSON framing. Blank lines are yielded too (callers skip
// them); a line past kMaxLine without a newline is an error, not a hang.
class LineDecoder {
public:
    static constexpr size_t kMaxLine = 64 * 1024 * 1024;
    void feed(std::string_view bytes) { buffer_.append(bytes); }
    std::optional<std::string> next() {
        if (!error_.empty()) return std::nullopt;
        const size_t end = buffer_.find('\n');
        if (end == std::string::npos) {
            if (buffer_.size() > kMaxLine) error_ = "message too large";
            return std::nullopt;
        }
        std::string line = buffer_.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        buffer_.erase(0, end + 1);
        return line;
    }
    const std::string& error() const { return error_; }

private:
    std::string buffer_;
    std::string error_;
};

// The conversation as the sidebar renders it. Streaming chunks merge into
// the trailing entry of the same kind; tool calls are keyed by toolCallId
// so tool_call_update lands on the row tool_call created.
enum class EntryKind { User, Agent, Thought, ToolCall, Plan, System, Error };
struct Entry {
    EntryKind kind = EntryKind::Agent;
    std::string text;        // body (chunks append here)
    std::string title;       // tool calls: human title
    std::string status;      // tool calls: pending|in_progress|completed|failed
    std::string toolCallId;
    std::string locationPath;  // tool calls: first diff/location path (click opens it)
    int locationLine = 0;
};

struct Transcript {
    std::vector<Entry> entries;
    void clear() { entries.clear(); }
    void user(const std::string& text);
    void system(const std::string& text);
    void error(const std::string& text);
    // One session/update's `update` object. Mode/config/info/usage updates
    // are consumed by Client state and never reach here.
    void apply_update(const nlohmann::json& update);
    std::string plain() const;

private:
    void chunk(EntryKind kind, const std::string& text);
    Entry* tool_call(const std::string& id);
    void describe_tool_content(Entry& entry, const nlohmann::json& update);
};

// fs/* serving, confined to the session root: an agent gets paths inside
// the repository it was started for, and an error for anything outside.
// Editor buffers intercept before these (see agent_panel.h); these are the
// on-disk fallback and the unit-tested policy.
struct FsRoot {
    std::filesystem::path root;  // canonicalised on construction via make()
    static FsRoot make(std::filesystem::path root);
    // The canonical target when `path` (absolute, or relative to root)
    // stays inside root; nullopt when it escapes (.., symlink, sibling).
    std::optional<std::filesystem::path> resolve(const std::string& path) const;
};
// `line` is 1-based (0/absent = start), `limit` 0 = to EOF.
std::optional<std::string> read_text_file(const FsRoot& root, const std::string& path,
                                          int line, int limit, std::string& error);
bool write_text_file(const FsRoot& root, const std::string& path, const std::string& content,
                     std::string& error);

struct Mode {
    std::string id, name, description;
};
// One row of session/list (agents that persist sessions).
struct SessionInfo {
    std::string id, cwd, title, updatedAt;
};
struct ConfigOption {
    std::string id, name, current, category;  // category e.g. "mode" | "model"
    std::vector<std::pair<std::string, std::string>> options;  // (value, name), select only
};
// Legacy unstable models list (session/new `models`, session/set_model);
// agents on the stable API expose the model as a config option instead.
struct ModelInfo {
    std::string id, name;
};
// A {type:"image", data, mimeType} prompt block read from disk.
// nullopt (with `error`) for a missing, oversized, or non-image file.
std::optional<nlohmann::json> image_block(const std::string& path, std::string& error);
std::string base64_encode(std::string_view bytes);
struct AuthMethod {
    std::string id, name, description;
};
struct PermissionOption {
    std::string id, name, kind;  // allow_once|allow_always|reject_once|reject_always
};

// Client lifecycle. Starting covers initialize + session/new (a cold `npx`
// fetch can take a while; the deadline is generous). Prompting returns to
// Ready on PromptDone. Dead = process ended, Failed = handshake/protocol.
enum class Phase { Unstarted, Starting, Ready, Prompting, Dead, Failed };
inline std::string_view phase_name(Phase phase) {
    switch (phase) {
        case Phase::Unstarted: return "Unstarted";
        case Phase::Starting: return "Starting";
        case Phase::Ready: return "Ready";
        case Phase::Prompting: return "Prompting";
        case Phase::Dead: return "Dead";
        case Phase::Failed: return "Failed";
    }
    return "Unstarted";
}

struct Event {
    enum class Kind { Update, Permission, FsRead, FsWrite, PromptDone, Log, Exited };
    Kind kind = Kind::Log;
    nlohmann::json update;   // Update: the `update` object of session/update
    nlohmann::json id;       // Permission/Fs*: request id to answer (int or string)
    nlohmann::json params;   // Permission/Fs*: raw params
    std::string text;        // PromptDone: stopReason. Log/Exited: message.
    bool failed = false;     // PromptDone: the prompt request errored (text says how)
};

class Client {
public:
    // Spawns the agent in `cwd` and starts initialize. Null (with `error`
    // set) only when the process cannot start at all; protocol failure
    // arrives later as Phase::Failed + an Exited event. `on_event` fires
    // from pump(), on the owner thread.
    static std::unique_ptr<Client> spawn(const AgentDef& agent, const std::string& cwd, std::string& error,
                                         std::chrono::milliseconds initialize_timeout = std::chrono::seconds(120));
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::function<void(const Event&)> on_event;

    void pump();  // drain queued messages; call every frame
    bool prompt(std::vector<nlohmann::json> blocks);  // Ready only
    void cancel();
    bool new_session();  // fresh session/new on the same process (Ready/Prompting)
    // Session persistence (capability-gated; false when unsupported).
    // list_sessions fills sessions() asynchronously; load_session replays
    // the conversation as session/update notifications, then is Ready.
    bool list_sessions();
    bool load_session(const std::string& sessionId);
    void set_mode(const std::string& modeId);
    void set_config_option(const std::string& configId, const std::string& value);
    void set_model(const std::string& modelId);
    void authenticate(const std::string& methodId);

    void answer_permission(const nlohmann::json& id, const std::string& optionId);  // "" = cancelled
    void answer_fs_read(const nlohmann::json& id, const std::string& content);
    void answer_fs_write(const nlohmann::json& id);
    void answer_error(const nlohmann::json& id, const std::string& message);

    Phase phase() const { return phase_; }
    bool alive() const { return alive_; }
    const std::string& session_id() const { return sessionId_; }
    const std::string& agent_name() const { return agentName_; }
    const std::string& agent_title() const { return sessionTitle_; }
    const std::string& error() const { return error_; }
    const std::vector<Mode>& modes() const { return modes_; }
    const std::string& current_mode() const { return currentMode_; }
    const std::vector<ConfigOption>& config_options() const { return configOptions_; }
    const std::vector<ModelInfo>& models() const { return models_; }
    const std::string& current_model() const { return currentModel_; }
    bool has_model_config_option() const {
        for (const auto& option : configOptions_)
            if (option.category == "model") return true;
        return false;
    }
    const std::vector<AuthMethod>& auth_methods() const { return authMethods_; }
    const std::vector<std::string>& available_commands() const { return availableCommands_; }
    bool embedded_context() const { return embeddedContext_; }
    bool image_prompts() const { return imagePrompts_; }
    bool auth_required() const { return authRequired_; }
    bool can_list_sessions() const { return canList_; }
    bool can_load_sessions() const { return canLoad_ || canResume_; }
    const std::vector<SessionInfo>& sessions() const { return sessions_; }
    // MCP servers handed to session/new and session/load (Settings).
    void set_mcp_servers(nlohmann::json servers) { mcpServers_ = std::move(servers); }
    pid_t pid() const { return pid_; }

private:
    Client() = default;
    void send(const nlohmann::json& message);
    int request(const std::string& method, nlohmann::json params);
    void notify(const std::string& method, nlohmann::json params);
    void read_loop(int fd, bool is_stdout);
    void handle(const nlohmann::json& message);
    void handle_response(const nlohmann::json& message);
    void finish_prompt(std::string stopReason, bool failed);
    void fail(std::string why);  // -> Failed/Dead + Exited event, once
    void apply_session_result(const nlohmann::json& result);
    void apply_config_options(const nlohmann::json& list);

    AgentDef agent_;
    std::string cwd_;
    pid_t pid_ = -1;
    int in_ = -1, out_ = -1, err_ = -1;
    std::thread reader_, errReader_;
    std::mutex write_, queue_mutex_;
    std::queue<std::string> lines_;       // stdout lines ("" sentinel = EOF)
    std::queue<std::string> errLines_;    // stderr lines for Log events
    std::atomic<bool> alive_{false};
    bool exited_ = false;
    std::string lastStderr_;

    // Client terminals (terminal/*): processes the agent asks the editor
    // to run. Output accumulates on a reader thread (tail kept past the
    // byte limit); exit is reaped in pump(), which also answers deferred
    // terminal/wait_for_exit requests. cwd is confined to the repo like fs.
    struct Terminal {
        std::string id;
        pid_t pid = -1;
        int out = -1;
        std::thread reader;
        std::mutex mutex;
        std::string buffer;
        bool truncated = false;
        size_t byteLimit = 1024 * 1024;
        bool exited = false;
        std::optional<int> exitCode;
        std::string signalName;
    };
    void handle_terminal(const std::string& method, const nlohmann::json& id, const nlohmann::json& params);
    void reap_terminals();
    void release_terminal(Terminal& terminal, bool killRunning);
    void terminal_output_result(const nlohmann::json& id, Terminal& terminal);
    std::map<std::string, std::unique_ptr<Terminal>> terminals_;
    std::vector<std::pair<nlohmann::json, std::string>> terminalWaiters_;  // (request id, terminal id)
    int nextTerminal_ = 1;

    Phase phase_ = Phase::Unstarted;
    enum class Purpose { Initialize, NewSession, Prompt, Authenticate, SetMode, SetConfig, SetModel, ListSessions, LoadSession };
    std::map<int, Purpose> pending_;
    int nextId_ = 1;
    std::chrono::steady_clock::time_point deadline_{};
    std::chrono::milliseconds initializeTimeout_ = std::chrono::seconds(120);

    std::string sessionId_, agentName_, sessionTitle_, error_, currentMode_, currentModel_, pendingModel_;
    std::vector<Mode> modes_;
    std::vector<ModelInfo> models_;
    std::vector<ConfigOption> configOptions_;
    std::vector<AuthMethod> authMethods_;
    std::vector<std::string> availableCommands_;
    std::vector<SessionInfo> sessions_;
    nlohmann::json mcpServers_ = nlohmann::json::array();
    bool embeddedContext_ = false;
    bool imagePrompts_ = false;
    bool authRequired_ = false;
    bool canLoad_ = false, canList_ = false, canResume_ = false;
};

}  // namespace acp
