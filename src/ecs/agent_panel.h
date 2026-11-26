#pragma once

// The ACP agent sidebar: an agent (Muse, Claude, Codex) runs as an ACP
// process per repository tab, streams into the transcript, and edits code
// through the editor — fs/read_text_file prefers a live edit buffer and
// fs/write_text_file replaces one, so agent edits appear in the editor as
// unsaved changes the user reviews like any other. Permission requests
// block on buttons in the panel. AgentPollSystem pumps every tab's client
// each frame, so agents keep working while another sidebar mode is shown.

#include <fstream>
#include <sstream>

#include "../settings.h"
#include <afterhours/src/plugins/ui/text_input/text_area.h>
#include "../ui/zoom.h"
#include "ui_imports.h"

#include <afterhours/src/plugins/ui/text_input/text_input.h>
#include <afterhours/src/plugins/ui/text_measure.h>

#ifdef __APPLE__
extern "C" bool metal_choose_image(char* out, int out_size);
#endif

namespace ecs {
namespace agent_panel {

inline void agent_notice(const std::string& message, MenuComponent::Notice::Kind kind = MenuComponent::Notice::Kind::Info) {
    if (auto* menu = find_singleton<MenuComponent>())
        menu->pendingToasts.push_back({message, kind});
}

inline void ensure_registry(AgentComponent& agent) {
    if (agent.agents.empty()) agent.agents = acp::default_agents();
    if (!agent.contextSeeded) {
        agent.agentId = Settings::get().get_acp_agent();
        agent.contextSeeded = true;
    }
    if (!acp::find_agent(agent.agents, agent.agentId))
        agent.agentId = agent.agents.empty() ? "" : agent.agents.front().id;
}

inline std::string relative_to(const RepoComponent& repo, const std::string& path) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(
        acp::FsRoot::make(repo.repoPath).resolve(path).value_or(std::filesystem::path{}),
        acp::FsRoot::make(repo.repoPath).root, ec);
    return ec || rel.empty() ? path : rel.string();
}

// Slice `text` the way fs/read_text_file asks (1-based line, 0 = to EOF).
inline std::string slice_lines(const std::string& text, int line, int limit) {
    if (line <= 1 && limit <= 0) return text;
    std::istringstream in(text);
    std::string out, current;
    int number = 1, taken = 0;
    while (std::getline(in, current)) {
        if (number++ < line) continue;
        out += current + "\n";
        if (limit > 0 && ++taken >= limit) break;
    }
    return out;
}

inline void serve_fs_read(RepoComponent& repo, AgentComponent& agent, const acp::Event& event) {
    const std::string path = event.params.value("path", "");
    const int line = event.params.value("line", 0);
    const int limit = event.params.value("limit", 0);
    // A live buffer wins over disk: the agent reads what the user sees,
    // unsaved edits included.
    if (auto buffered = navigation::edit_buffer_text_for(repo, relative_to(repo, path))) {
        agent.client->answer_fs_read(event.id, slice_lines(*buffered, line, limit));
        return;
    }
    std::string error;
    if (auto content = acp::read_text_file(acp::FsRoot::make(repo.repoPath), path, line, limit, error))
        agent.client->answer_fs_read(event.id, *content);
    else agent.client->answer_error(event.id, error);
}

inline void serve_fs_write(RepoComponent& repo, AgentComponent& agent, const acp::Event& event) {
    const std::string path = event.params.value("path", "");
    const std::string content = event.params.value("content", "");
    const std::string rel = relative_to(repo, path);
    // Into an open editor buffer, the write is live and unsaved (Cmd+S
    // still belongs to the user); otherwise it lands on disk and the next
    // refresh shows it in the diff like any external edit.
    if (navigation::apply_external_edit(repo, rel, content)) {
        agent_notice("Agent edited " + rel + " (unsaved)");
        agent.client->answer_fs_write(event.id);
        return;
    }
    std::string error;
    if (acp::write_text_file(acp::FsRoot::make(repo.repoPath), path, content, error)) {
        agent_notice("Agent wrote " + rel);
        repo.request_refresh();
        agent.client->answer_fs_write(event.id);
    } else {
        agent.transcript.error(error);
        agent.client->answer_error(event.id, error);
    }
}

inline void handle_event(RepoComponent& repo, AgentComponent& agent, const acp::Event& event) {
    switch (event.kind) {
        case acp::Event::Kind::Update: agent.transcript.apply_update(event.update); break;
        case acp::Event::Kind::FsRead: serve_fs_read(repo, agent, event); break;
        case acp::Event::Kind::FsWrite: serve_fs_write(repo, agent, event); break;
        case acp::Event::Kind::Permission: {
            AgentComponent::Permission permission;
            permission.id = event.id;
            const auto toolCall = event.params.value("toolCall", nlohmann::json::object());
            permission.title = toolCall.value("title", "Run a tool");
            const auto options = event.params.value("options", nlohmann::json::array());
            if (options.is_array())
                for (const auto& option : options)
                    permission.options.push_back({option.value("optionId", ""), option.value("name", ""),
                                                   option.value("kind", "")});
            if (agent.permission)  // replaced before being answered: decline the old one
                agent.client->answer_permission(agent.permission->id, "");
            agent.permission = std::move(permission);
            break;
        }
        case acp::Event::Kind::PromptDone:
            if (event.failed) agent.transcript.error(event.text);
            else if (event.text == "cancelled") agent.transcript.system("Cancelled.");
            else if (event.text == "refusal") agent.transcript.system("The agent declined.");
            break;
        case acp::Event::Kind::Log: break;  // stderr; Exited carries the useful tail
        case acp::Event::Kind::Exited:
            agent.lastError = event.text;
            agent.transcript.error(event.text);
            agent.permission.reset();
            break;
    }
}

inline void stop(AgentComponent& agent) {
    if (agent.client) {
        agent.client->on_event = nullptr;
        agent.client.reset();
    }
    agent.permission.reset();
    agent.clientAgentId.clear();
    agent.clientRepo.clear();
}

inline bool start(RepoComponent& repo, AgentComponent& agent, const std::string& agentId) {
    ensure_registry(agent);
    const auto* def = acp::find_agent(agent.agents, agentId);
    if (!def) { agent.transcript.error("Unknown agent: " + agentId); return false; }
    if (repo.repoPath.empty()) { agent.transcript.error("Open a repository before starting an agent."); return false; }
    stop(agent);
    agent.agentId = agentId;
    Settings::get().set_acp_agent(agentId);
    std::string error;
    auto client = acp::Client::spawn(*def, repo.repoPath, error);
    if (!client) {
        agent.transcript.error("Could not start " + def->name + ": " + error);
        agent.lastError = error;
        return false;
    }
    agent.client = std::move(client);
    // Before the first pump turns initialize into session/new.
    agent.client->set_mcp_servers(Settings::get().get_acp_mcp_servers());
    agent.clientAgentId = def->id;
    agent.clientRepo = repo.repoPath;
    agent.lastError.clear();
    // Fires only from Client::pump(), which AgentPollSystem calls with
    // these very components alive; stop() clears it before teardown.
    RepoComponent* repoPtr = &repo;
    AgentComponent* agentPtr = &agent;
    agent.client->on_event = [repoPtr, agentPtr](const acp::Event& event) {
        handle_event(*repoPtr, *agentPtr, event);
    };
    agent.transcript.system(def->name + " starting…");
    return true;
}

// The prompt blocks for one send: the user's text plus, when enabled, the
// open source file so "fix this" needs no path. Embedded-context agents
// get an ACP resource; others get a fenced block appended to the text.
inline std::vector<nlohmann::json> prompt_blocks(RepoComponent& repo, AgentComponent& agent,
                                                 const std::string& text) {
    std::vector<nlohmann::json> blocks;
    std::string body = text;
    if (agent.includeContext && agent.client) {
        const std::string path = repo.fullFilePath();
        if (!path.empty() && repo.fullFileRevision().empty()) {
            std::optional<std::string> fileText = navigation::edit_buffer_text_for(repo, path);
            if (!fileText) {
                std::string error;
                fileText = acp::read_text_file(acp::FsRoot::make(repo.repoPath), path, 0, 0, error);
            }
            if (fileText && fileText->size() <= 256 * 1024) {
                const std::string uri = "file://" + (std::filesystem::path(repo.repoPath) / path).string();
                if (agent.client->embedded_context())
                    blocks.push_back({{"type", "resource"},
                                      {"resource", {{"uri", uri}, {"mimeType", "text/plain"}, {"text", *fileText}}}});
                else body += "\n\nCurrent file `" + path + "`:\n```\n" + *fileText + "\n```\n";
            }
        }
    }
    // Attached images ride this one prompt (consumed even when one file
    // fails, so a bad path is reported once, not on every send).
    if (!agent.pendingImages.empty()) {
        if (agent.client && agent.client->image_prompts()) {
            for (const auto& path : agent.pendingImages) {
                std::string error;
                if (auto block = acp::image_block(path, error)) blocks.push_back(std::move(*block));
                else agent.transcript.error(error);
            }
        } else agent.transcript.error("This agent does not accept image prompts; attachments dropped.");
        agent.pendingImages.clear();
    }
    // Named value, not a braced list: vector::insert would pick its
    // initializer_list overload and splat the object's pairs as elements.
    nlohmann::json textBlock{{"type", "text"}, {"text", body}};
    blocks.insert(blocks.begin(), std::move(textBlock));
    return blocks;
}

inline bool send_prompt(RepoComponent& repo, AgentComponent& agent, const std::string& text) {
    if (text.empty()) return false;
    ensure_registry(agent);
    if (!agent.client || agent.client->phase() == acp::Phase::Dead ||
        agent.client->phase() == acp::Phase::Failed) {
        if (!start(repo, agent, agent.agentId)) return false;
        agent.pendingPrompt = text;
        return true;
    }
    if (agent.client->phase() == acp::Phase::Starting) {
        agent.pendingPrompt = text;
        return true;
    }
    if (agent.client->phase() != acp::Phase::Ready) {
        agent.transcript.system("The agent is not ready (" +
                                std::string(acp::phase_name(agent.client->phase())) + ").");
        return false;
    }
    agent.transcript.user(text);
    return agent.client->prompt(prompt_blocks(repo, agent, text));
}

inline bool send_feedback(RepoComponent& repo, AgentComponent& agent, const ReviewComponent& review) {
    const std::string markdown = build_review_markdown(review, repo.currentBranch);
    if (markdown.empty()) return false;
    return send_prompt(repo, agent, "Address this code review feedback. Make the edits in the "
                                    "working tree, then summarize what you changed.\n\n" + markdown);
}

inline void poll(RepoComponent& repo, AgentComponent& agent) {
    if (!agent.client) return;
    // A tab's repository can be relinked or swapped under its agent; the
    // process serves the repo it was spawned for, so stop it rather than
    // let it edit the wrong tree.
    if (!agent.clientRepo.empty() && agent.clientRepo != repo.repoPath) {
        agent.transcript.system("Repository changed; agent stopped.");
        stop(agent);
        return;
    }
    agent.client->pump();
    if (!agent.pendingPrompt.empty() && agent.client && agent.client->phase() == acp::Phase::Ready) {
        std::string text = std::move(agent.pendingPrompt);
        agent.pendingPrompt.clear();
        agent.transcript.system(agent.client->agent_name().empty() ? "Connected."
                                                                   : "Connected to " + agent.client->agent_name() + ".");
        agent.transcript.user(text);
        agent.client->prompt(prompt_blocks(repo, agent, text));
    }
}

// ---------------------------------------------------------------------- UI

inline std::string entry_body(const acp::Entry& entry, std::string& prefix) {
    switch (entry.kind) {
        case acp::EntryKind::User: prefix = ""; break;
        case acp::EntryKind::Thought: prefix = "… "; break;
        case acp::EntryKind::ToolCall:
            prefix = "";
            return (entry.title.empty() ? "tool" : entry.title) +
                   (entry.status.empty() ? "" : " · " + entry.status) +
                   (entry.text.empty() ? "" : "\n" + entry.text);
        case acp::EntryKind::System: prefix = ""; break;
        case acp::EntryKind::Error: prefix = "Error · "; break;
        case acp::EntryKind::Agent:
        case acp::EntryKind::Plan: prefix = ""; break;
    }
    return entry.text;
}

inline afterhours::Color entry_color(const acp::Entry& entry) {
    switch (entry.kind) {
        case acp::EntryKind::User: return theme::TEXT_ACCENT;
        case acp::EntryKind::Agent: return theme::TEXT_PRIMARY;
        case acp::EntryKind::Thought: return theme::TEXT_TERTIARY;
        case acp::EntryKind::ToolCall:
            if (entry.status == "completed") return theme::STATUS_ADDED;
            if (entry.status == "failed") return theme::STATUS_DELETED;
            return theme::STATUS_MODIFIED;
        case acp::EntryKind::Plan: return theme::TEXT_SECONDARY;
        case acp::EntryKind::System: return theme::TEXT_TERTIARY;
        case acp::EntryKind::Error: return theme::STATUS_DELETED;
    }
    return theme::TEXT_PRIMARY;
}

inline afterhours::Color status_dot_color(const AgentComponent& agent) {
    if (!agent.client) return theme::TEXT_TERTIARY;
    switch (agent.client->phase()) {
        case acp::Phase::Ready: return theme::STATUS_ADDED;
        case acp::Phase::Starting:
        case acp::Phase::Prompting: return theme::STATUS_MODIFIED;
        case acp::Phase::Dead:
        case acp::Phase::Failed: return theme::STATUS_DELETED;
        case acp::Phase::Unstarted: return theme::TEXT_TERTIARY;
    }
    return theme::TEXT_TERTIARY;
}

inline std::string status_label(const AgentComponent& agent) {
    if (!agent.client) return "Not started";
    switch (agent.client->phase()) {
        case acp::Phase::Unstarted: return "Not started";
        case acp::Phase::Starting: return "Starting…";
        case acp::Phase::Ready:
            return agent.client->auth_required() ? "Sign in needed" : "Ready";
        case acp::Phase::Prompting: return "Working…";
        case acp::Phase::Dead: return "Exited";
        case acp::Phase::Failed: return "Failed";
    }
    return "";
}

// Rendered by SidebarSystem in Agent mode, under the repo header and the
// mode tabs. `height` is the space left in the sidebar.
inline void render(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo,
                   LayoutComponent& layout, AgentComponent& agent, float width, float height) {
    (void)layout;
    ensure_registry(agent);
    // Wide/full-window guard: the companion column never stretches past
    // 760px (shelf-collapsed sidebar = whole window); it centres instead.
    const float panelW = std::min(width, 760.f);
    const float panelOffsetX = std::max(0.f, (width - panelW) * 0.5f);
    const float innerW = std::max(40.f, panelW - 16.f);
    const float zoom = ::ui::zoom::get();

    auto panel = div(ctx, mk(parent, 790000), ComponentConfig{}
        .with_size(ComponentSize{pixels(panelW), pixels(height)})
        .with_margin(Margin{.left = pixels(panelOffsetX)})
        .with_flex_direction(FlexDirection::Column).with_no_wrap()
        .with_padding(Padding{.top = pixels(6), .right = pixels(8), .bottom = pixels(8), .left = pixels(8)})
        .with_gap(pixels(6))
        .with_debug_name("agent_panel"));

    // Status row.
    auto statusRow = div(ctx, mk(panel.ent(), 1), ComponentConfig{}
        .with_size(ComponentSize{pixels(innerW), pixels(22)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_align_items(AlignItems::Center)
        .with_debug_name("agent_status_row"));
    const std::string status = status_label(agent) +
        (agent.client && !agent.client->agent_title().empty() ? " · " + agent.client->agent_title() : "");
    div(ctx, mk(statusRow.ent(), 9), ComponentConfig{}
        .with_size(ComponentSize{pixels(8), pixels(8)})
        .with_custom_background(status_dot_color(agent))
        .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
        .with_roundness(1.f)
        .with_margin(Margin{.right = pixels(2)})
        .with_debug_name("agent_status_dot"));
    div(ctx, mk(statusRow.ent(), 0), ComponentConfig{}
        .with_label(status).with_size(ComponentSize{expand(), pixels(22)})
        .with_custom_text_color(theme::TEXT_SECONDARY).with_font_size(pixels(12))
        .with_alignment(TextAlignment::Left).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("agent_status"));
    float usedH = 28.f;

    // Agent picker.
    auto pickerRow = div(ctx, mk(panel.ent(), 2), ComponentConfig{}
        .with_size(ComponentSize{pixels(innerW), pixels(28)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
        .with_debug_name("agent_picker"));
    int pickerId = 0;
    for (const auto& def : agent.agents) {
        const bool selected = def.id == agent.agentId;
        auto config = preset::Button(def.name, def.available)
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
            .with_tooltip(def.description + (def.available ? "" : " (not found on PATH)"))
            .with_debug_name("agent_pick_" + def.id);
        if (selected) config = config.with_custom_background(theme::BUTTON_PRIMARY)
                                  .with_custom_text_color(theme::BUTTON_PRIMARY_TEXT)
                                  .with_font("ui-bold", pixels(12));
        if (button(ctx, mk(pickerRow.ent(), pickerId++), config) && !selected) {
            agent.agentId = def.id;
            Settings::get().set_acp_agent(def.id);
            if (agent.client && agent.clientAgentId != def.id) {
                stop(agent);
                agent.transcript.system("Switched to " + def.name + ".");
            }
        }
    }
    usedH += 34.f;

    // Controls: start/stop, new session, mode/config cycles, sign-in.
    auto controlsRow = div(ctx, mk(panel.ent(), 3), ComponentConfig{}
        .with_size(ComponentSize{pixels(innerW), pixels(28)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
        .with_debug_name("agent_controls"));
    const bool running = agent.client && agent.client->alive();
    if (button(ctx, mk(controlsRow.ent(), 0), preset::Button(running ? "Stop" : "Start")
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
            .with_debug_name("agent_start_stop"))) {
        if (running) { stop(agent); agent.transcript.system("Stopped."); }
        else start(repo, agent, agent.agentId);
    }
    if (button(ctx, mk(controlsRow.ent(), 1), preset::Button("New", running)
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
            .with_tooltip("Fresh conversation on the same agent process")
            .with_debug_name("agent_new"))) {
        agent.transcript.clear();
        agent.permission.reset();
        agent.showSessions = false;
        agent.client->new_session();
    }
    const bool canResume = agent.client && agent.client->can_list_sessions() &&
                           agent.client->can_load_sessions();
    if (canResume && button(ctx, mk(controlsRow.ent(), 2), preset::Button("Resume")
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
            .with_tooltip("Continue a previous conversation with this agent")
            .with_debug_name("agent_resume"))) {
        agent.showSessions = !agent.showSessions;
        if (agent.showSessions) agent.client->list_sessions();
    }
    usedH += 34.f;

    if (agent.showSessions && agent.client) {
        const auto& sessions = agent.client->sessions();
        if (sessions.empty()) {
            div(ctx, mk(panel.ent(), 25), ComponentConfig{}
                .with_label("No saved sessions for this repository.")
                .with_size(ComponentSize{pixels(innerW), pixels(20)})
                .with_custom_text_color(theme::TEXT_TERTIARY).with_font_size(pixels(12))
                .with_alignment(TextAlignment::Left)
                .with_debug_name("agent_sessions_empty"));
            usedH += 26.f;
        }
        int row = 0;
        for (const auto& session : sessions) {
            if (row >= 6) break;
            const std::string label = session.title.empty() ? session.id : session.title;
            if (button(ctx, mk(panel.ent(), 26 + row), preset::Button(label)
                    .with_size(ComponentSize{pixels(innerW), pixels(24)}).with_font_size(pixels(12))
                    .with_alignment(TextAlignment::Left)
                    .with_tooltip(session.id + (session.updatedAt.empty() ? "" : "\n" + session.updatedAt))
                    .with_debug_name("agent_session_" + std::to_string(row)))) {
                agent.transcript.clear();
                agent.permission.reset();
                agent.showSessions = false;
                if (!agent.client->load_session(session.id))
                    agent.transcript.system("That session could not be resumed.");
            }
            ++row;
            usedH += 30.f;
        }
    }

    // Compact settings chips: one header row, not stacked full-width bars.
    auto chipsRow = div(ctx, mk(panel.ent(), 8), ComponentConfig{}
        .with_size(ComponentSize{pixels(innerW), pixels(24)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
        .with_align_items(AlignItems::Center)
        .with_debug_name("agent_chips"));
    int chipId = 0;
    auto chip_config = [](const std::string& label, bool enabled) {
        return preset::Button(label, enabled)
            .with_size(ComponentSize{children(), pixels(24)})
            .with_padding(Padding{.top = pixels(4), .right = pixels(8), .bottom = pixels(4), .left = pixels(8)})
            .with_font_size(pixels(12))
            .with_transparent_bg()
            .with_border(theme::BORDER, pixels(1))
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_alignment(TextAlignment::Center);
    };

    if (agent.client) {
        // Config-option cycles (Muse's mode selector arrives this way);
        // the legacy modes list (Claude/Codex adapters) cycles instead.
        int optionId = 0;
        for (const auto& option : agent.client->config_options()) {
            std::string current = option.current;
            for (const auto& [value, name] : option.options) if (value == option.current) current = name;
            if (button(ctx, mk(chipsRow.ent(), chipId++),
                       chip_config(option.name + ": " + current, option.options.size() > 1)
                           .with_debug_name("agent_config_" + option.id))) {
                size_t at = 0;
                for (size_t i = 0; i < option.options.size(); ++i)
                    if (option.options[i].first == option.current) at = i;
                agent.client->set_config_option(option.id, option.options[(at + 1) % option.options.size()].first);
            }
            ++optionId;
        }
        // Legacy models list (the stable API puts model in config options,
        // which the generic cycle above already renders).
        if (!agent.client->models().empty() && !agent.client->has_model_config_option()) {
            std::string current = agent.client->current_model();
            for (const auto& model : agent.client->models()) if (model.id == current) current = model.name;
            if (button(ctx, mk(chipsRow.ent(), chipId++), chip_config("Model: " + current, agent.client->models().size() > 1)
                    .with_debug_name("agent_model"))) {
                const auto& models = agent.client->models();
                size_t at = 0;
                for (size_t i = 0; i < models.size(); ++i)
                    if (models[i].id == agent.client->current_model()) at = i;
                agent.client->set_model(models[(at + 1) % models.size()].id);
            }
        }
        if (agent.client->config_options().empty() && !agent.client->modes().empty()) {
            std::string current = agent.client->current_mode();
            for (const auto& mode : agent.client->modes()) if (mode.id == current) current = mode.name;
            if (button(ctx, mk(chipsRow.ent(), chipId++), chip_config("Mode: " + current, agent.client->modes().size() > 1)
                    .with_debug_name("agent_mode"))) {
                const auto& modes = agent.client->modes();
                size_t at = 0;
                for (size_t i = 0; i < modes.size(); ++i)
                    if (modes[i].id == agent.client->current_mode()) at = i;
                agent.client->set_mode(modes[(at + 1) % modes.size()].id);
            }
        }
        if (agent.client->auth_required() && !agent.client->auth_methods().empty()) {
            const auto& method = agent.client->auth_methods().front();
            if (button(ctx, mk(panel.ent(), 21), preset::Button("Sign in: " + method.name)
                    .with_size(ComponentSize{pixels(innerW), pixels(26)}).with_font_size(pixels(12))
                    .with_tooltip(method.description)
                    .with_debug_name("agent_sign_in")))
                agent.client->authenticate(method.id);
            usedH += 32.f;
        }
    }

    // MCP servers: the configured list rides session/new+load; edited as
    // raw JSON (the ACP McpServer shapes), validated as an array on save.
    {
        const auto servers = Settings::get().get_acp_mcp_servers();
        if (button(ctx, mk(chipsRow.ent(), chipId++),
                   chip_config("MCP (" + std::to_string(servers.size()) + ")", true)
                       .with_tooltip("MCP servers handed to new/loaded sessions")
                       .with_debug_name("agent_mcp_toggle"))) {
            agent.showMcpEditor = !agent.showMcpEditor;
            if (agent.showMcpEditor) agent.mcpDraft = servers.dump(2);
        }
        usedH += 30.f;
        if (agent.showMcpEditor) {
            afterhours::text_input::text_area(ctx, mk(panel.ent(), 24), agent.mcpDraft,
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(innerW), pixels(84)})
                    .with_custom_background(theme::INPUT_BG)
                    .with_border(theme::BORDER, pixels(1.f))
                    .with_corner_radius(theme::layout::RADIUS_CONTROL)
                    .with_font("mono", pixels(12))
                    .with_line_height(pixels(15.f))
                    .with_placeholder("[{\"name\": \"server\", \"command\": \"...\"}]")
                    .with_debug_name("agent_mcp_input"));
            if (button(ctx, mk(panel.ent(), 25), preset::Button("Save MCP servers")
                    .with_size(ComponentSize{pixels(innerW), pixels(24)}).with_font_size(pixels(12))
                    .with_debug_name("agent_mcp_save"))) {
                const auto parsed = nlohmann::json::parse(agent.mcpDraft, nullptr, false);
                if (parsed.is_array()) {
                    Settings::get().set_acp_mcp_servers(parsed);
                    if (agent.client) agent.client->set_mcp_servers(parsed);
                    agent.transcript.system("MCP servers saved (" + std::to_string(parsed.size()) +
                                            "); they apply to the next new or resumed session.");
                    agent.showMcpEditor = false;
                } else agent.transcript.error("MCP servers must be a JSON array.");
            }
            usedH += 114.f;
        }
    }

    // Review feedback: the basket's markdown, straight to the agent.
    if (auto* review = find_singleton<ReviewComponent, ActiveTab>()) {
        const int unresolved = unresolved_comment_count(*review);
        if (unresolved > 0) {
            if (button(ctx, mk(panel.ent(), 22),
                       preset::Button("Send review feedback (" + std::to_string(unresolved) + ")")
                           .with_size(ComponentSize{pixels(innerW), pixels(26)}).with_font_size(pixels(12))
                           .with_debug_name("agent_send_feedback")))
                send_feedback(repo, agent, *review);
            usedH += 32.f;
        }
    }

    // Transcript fills what is left; entries are measured like the
    // feedback basket's comment bodies so rows size to wrapped text.
    const float permReserve = agent.permission ? 68.f : 0.f;
    const float imageReserve = static_cast<float>(std::min<size_t>(agent.pendingImages.size(), 4)) * 28.f;
    constexpr float kComposerReserve = 56.f + 6.f + 34.f;
    const float transcriptH = std::max(40.f, height - usedH - permReserve - imageReserve - kComposerReserve - 14.f);
    auto scroll = div(ctx, mk(panel.ent(), 50), preset::ScrollPanel()
        .with_size(ComponentSize{pixels(innerW), pixels(transcriptH)})
        .with_debug_name("agent_transcript"));
    auto& measure = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::TextMeasureCache>();
    constexpr float kFont = 14.f;
    constexpr float kSmallFont = 12.f;
    size_t totalChars = 0;
    if (agent.transcript.entries.empty()) {
        const auto* def = acp::find_agent(agent.agents, agent.agentId);
        div(ctx, mk(scroll.ent(), 0), ComponentConfig{}
            .with_label(def ? def->description + "\n\nStart the agent, then ask for a change. "
                              "Edits land in the editor and working tree for review."
                            : "No agents available.")
            .with_size(ComponentSize{pixels(innerW - 8.f), pixels(72)})
            .with_custom_text_color(theme::TEXT_TERTIARY).with_font_size(pixels(12))
            .with_alignment(TextAlignment::Left)
            .with_debug_name("agent_empty"));
    }
    int entryId = 0;
    for (const auto& entry : agent.transcript.entries) {
        std::string prefix;
        const std::string body = entry_body(entry, prefix);
        const std::string text = prefix + body;
        totalChars += text.size();
        const std::string font = entry.kind == acp::EntryKind::ToolCall ? "mono"
                                 : afterhours::ui::UIComponent::DEFAULT_FONT;
        const float entryFont = (entry.kind == acp::EntryKind::User || entry.kind == acp::EntryKind::Agent)
            ? kFont : kSmallFont;
        const bool carded = entry.kind == acp::EntryKind::User || entry.kind == acp::EntryKind::ToolCall;
        const float entryPadX = carded ? 16.f : 4.f;
        const float textH = afterhours::ui::measure_text_wrapped(
            measure, text, font, entryFont * zoom, std::max(40.f, innerW - 8.f - entryPadX) * zoom).height / zoom + (carded ? 16.f : 6.f);
        auto config = ComponentConfig{}
            .with_label(text)
            .with_size(ComponentSize{pixels(innerW - 8.f), pixels(std::max(20.f, textH))})
            .with_custom_text_color(entry.kind == acp::EntryKind::User ? theme::TEXT_PRIMARY : entry_color(entry))
            .with_font(font, pixels(entryFont))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{.top = pixels(carded ? 8 : 2), .right = pixels(8), .bottom = pixels(carded ? 8 : 2), .left = pixels(8)})
            .with_margin(Margin{.bottom = pixels(6)})
            .with_debug_name("agent_entry");
        if (entry.kind == acp::EntryKind::User)
            config = config.with_custom_background(theme::SELECTED_BG)
                           .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                           .with_corner_radius(theme::layout::RADIUS_CONTROL);
        else if (entry.kind == acp::EntryKind::ToolCall)
            config = config.with_custom_background(theme::INPUT_BG)
                           .with_border(theme::BORDER, pixels(1))
                           .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                           .with_corner_radius(theme::layout::RADIUS_CONTROL);
        else
            config = config.with_transparent_bg();
        // A tool call with a file location opens it (preview), like the
        // feedback basket's location links.
        if (entry.kind == acp::EntryKind::ToolCall && !entry.locationPath.empty()) {
            auto openBtn = button(ctx, mk(scroll.ent(), 100 + entryId++),
                config.with_debug_name("agent_entry_open"));
            if (openBtn) {
                const std::string rel = relative_to(repo, entry.locationPath);
                navigation::preview(repo, reading::Location{
                    reading::source(rel, "", entry.locationLine)});
            }
        } else {
            div(ctx, mk(scroll.ent(), 100 + entryId++), config);
        }
    }
    if (totalChars != agent.seenTranscriptChars) {
        agent.seenTranscriptChars = totalChars;
        if (scroll.ent().has<afterhours::ui::HasScrollView>())
            scroll.ent().get<afterhours::ui::HasScrollView>().scroll_to_bottom();
    }
    // Permission card: amber left accent, side-by-side compact actions.
    if (agent.permission) {
        auto banner = div(ctx, mk(panel.ent(), 30), ComponentConfig{}
            .with_size(ComponentSize{pixels(innerW), pixels(62)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap()
            .with_custom_background(theme::INPUT_BG).with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_corner_radius(theme::layout::RADIUS_CARD)
            .with_overflow(Overflow::Hidden)
            .with_debug_name("agent_permission"));
        div(ctx, mk(banner.ent(), 90), ComponentConfig{}
            .with_size(ComponentSize{pixels(3), percent(1.f)})
            .with_custom_background(theme::STATUS_MODIFIED)
            .with_roundness(0.f)
            .with_debug_name("agent_permission_accent"));
        auto permBody = div(ctx, mk(banner.ent(), 91), ComponentConfig{}
            .with_size(ComponentSize{expand(), percent(1.f)})
            .with_flex_direction(FlexDirection::Column).with_no_wrap().with_gap(pixels(6))
            .with_padding(Padding{.top = pixels(8), .right = pixels(8), .bottom = pixels(8), .left = pixels(8)})
            .with_transparent_bg()
            .with_debug_name("agent_permission_body"));
        div(ctx, mk(permBody.ent(), 0), ComponentConfig{}
            .with_label("Allow: " + agent.permission->title)
            .with_size(ComponentSize{percent(1.f), pixels(18)})
            .with_custom_text_color(theme::TEXT_PRIMARY).with_font_size(pixels(12))
            .with_alignment(TextAlignment::Left).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("agent_permission_title"));
        auto permActions = div(ctx, mk(permBody.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(28)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(8))
            .with_debug_name("agent_permission_actions"));
        int optionIdx = 0;
        std::string chosen;
        bool answered = false, cancelled = false;
        for (const auto& option : agent.permission->options) {
            const bool reject = option.kind.rfind("reject", 0) == 0;
            auto config = preset::Button(option.name)
                .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
                .with_debug_name("agent_permission_" + option.id);
            if (!reject) config = config.with_custom_background(theme::BUTTON_PRIMARY)
                .with_custom_text_color(theme::BUTTON_PRIMARY_TEXT)
                .with_font("ui-bold", pixels(12));
            else config = config.with_transparent_bg().with_border(theme::BORDER, pixels(1))
                .with_custom_text_color(theme::TEXT_SECONDARY);
            if (button(ctx, mk(permActions.ent(), optionIdx), config)) { chosen = option.id; answered = true; }
            ++optionIdx;
        }
        if (agent.permission->options.empty())
            cancelled = button(ctx, mk(permActions.ent(), 9), preset::Button("Dismiss")
                .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
                .with_debug_name("agent_permission_dismiss"));
        if ((answered || cancelled) && agent.client) {
            agent.client->answer_permission(agent.permission->id, answered ? chosen : "");
            agent.permission.reset();
        }
        usedH += 68.f;
    }

    // Pending image attachments (ride the next prompt).
    for (size_t i = 0; i < agent.pendingImages.size() && i < 4; ++i) {
        auto chip = div(ctx, mk(panel.ent(), 60 + static_cast<int>(i)), ComponentConfig{}
            .with_size(ComponentSize{pixels(innerW), pixels(22)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
            .with_align_items(AlignItems::Center)
            .with_debug_name("agent_image_chip"));
        const std::string name = std::filesystem::path(agent.pendingImages[i]).filename().string();
        div(ctx, mk(chip.ent(), 0), ComponentConfig{}
            .with_label("🖼 " + name).with_size(ComponentSize{expand(), pixels(22)})
            .with_custom_text_color(theme::TEXT_SECONDARY).with_font_size(pixels(12))
            .with_alignment(TextAlignment::Left).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("agent_image_name"));
        if (button(ctx, mk(chip.ent(), 1), preset::Button("×")
                .with_size(ComponentSize{pixels(24), pixels(22)}).with_font_size(pixels(12))
                .with_debug_name("agent_image_remove"))) {
            agent.pendingImages.erase(agent.pendingImages.begin() + static_cast<long>(i));
            break;  // indices shifted; next frame redraws
        }
        usedH += 28.f;
    }

    // Prompt box.
    constexpr float kPromptH = 56.f;
    const bool prompting = agent.phase() == acp::Phase::Prompting;
    auto promptArea = afterhours::text_input::text_area(ctx, mk(panel.ent(), 40), agent.promptText,
        ComponentConfig{}
            .with_size(ComponentSize{pixels(innerW), pixels(kPromptH)})
            .with_custom_background(theme::INPUT_BG)
            .with_border(theme::BORDER, pixels(1.f))
            .with_corner_radius(theme::layout::RADIUS_CONTROL)
            .with_padding(Padding{.top = pixels(6), .right = pixels(8), .bottom = pixels(6), .left = pixels(8)})
            .with_line_height(pixels(20.f))
            .with_placeholder("Ask the agent to change, explain, or review code…")
            .with_font_size(pixels(14))
            .with_submit_on_enter()
            .with_debug_name("agent_prompt_input"));
    promptArea.ent().addComponentIfMissing<afterhours::text_input::HasTextInputListener>();
    promptArea.ent().get<afterhours::text_input::HasTextInputListener>().on_submit = [](Entity&) {
        auto* repoPtr = find_singleton<RepoComponent, ActiveTab>();
        auto* agentPtr = find_singleton<AgentComponent, ActiveTab>();
        if (repoPtr && agentPtr && send_prompt(*repoPtr, *agentPtr, agentPtr->promptText))
            agentPtr->promptText.clear();
    };
    usedH += kPromptH + 6.f;

    auto sendRow = div(ctx, mk(panel.ent(), 41), ComponentConfig{}
        .with_size(ComponentSize{pixels(innerW), pixels(28)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
        .with_debug_name("agent_send_row"));
    {
        auto sendConfig = preset::Button(prompting ? "Cancel" : "Send", prompting || !agent.promptText.empty())
            .with_size(ComponentSize{prompting ? children() : expand(), pixels(28)}).with_font_size(pixels(12))
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(4), .left = pixels(12)})
            .with_debug_name("agent_send");
        if (!prompting && !agent.promptText.empty())
            sendConfig = sendConfig.with_custom_background(theme::BUTTON_PRIMARY)
                .with_custom_text_color(theme::BUTTON_PRIMARY_TEXT)
                .with_font("ui-bold", pixels(12));
        if (prompting)
            sendConfig = sendConfig.with_transparent_bg().with_border(theme::BORDER, pixels(1))
                .with_custom_text_color(theme::DESTRUCTIVE_TEXT);
        if (button(ctx, mk(sendRow.ent(), 0), sendConfig)) {
        if (prompting) { if (agent.client) agent.client->cancel(); }
        else if (send_prompt(repo, agent, agent.promptText)) agent.promptText.clear();
    }
    }
    if (button(ctx, mk(sendRow.ent(), 1),
               preset::Button(agent.includeContext ? "Include file ✓" : "Include file", true)
                   .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
                   .with_custom_background(agent.includeContext ? theme::SELECTED_BG : theme::BUTTON_SECONDARY)
                   .with_custom_text_color(agent.includeContext ? theme::TEXT_ACCENT : theme::TEXT_SECONDARY)
                   .with_tooltip("Attach the open file to each prompt")
                   .with_debug_name("agent_include_file")))
        agent.includeContext = !agent.includeContext;
    const bool canImages = agent.client && agent.client->image_prompts();
    if (button(ctx, mk(sendRow.ent(), 2),
               preset::Button("Image", canImages)
                   .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
                   .with_tooltip(canImages ? "Attach an image to the next prompt"
                                           : "This agent does not accept image prompts")
                   .with_debug_name("agent_attach_image"))) {
        if (auto* menu = find_singleton<MenuComponent>())
            menu->pendingDialog = MenuComponent::PendingDialog::AttachImage;
    }
    usedH += 34.f;


}

}  // namespace agent_panel

// Pumps every tab's agent each frame, visible sidebar or not.
struct AgentPollSystem : afterhours::System<AgentComponent> {
    void for_each_with(afterhours::Entity& entity, AgentComponent& agent, float) override {
        if (!entity.has<RepoComponent>()) return;
        // The image picker is a modal native panel; run it outside the
        // render pass, for the active tab, like Open Repository. Test
        // runs point FH_TEST_ATTACH_IMAGE at a file instead.
        if (entity.has<ActiveTab>()) {
            if (auto* menu = find_singleton<MenuComponent>();
                menu && menu->pendingDialog == MenuComponent::PendingDialog::AttachImage) {
                menu->pendingDialog = MenuComponent::PendingDialog::None;
                std::string chosen;
                if (const char* path = std::getenv("FH_TEST_ATTACH_IMAGE")) chosen = path;
                else {
#ifdef __APPLE__
                    char buffer[4096] = {};
                    if (metal_choose_image(buffer, static_cast<int>(sizeof(buffer)))) chosen = buffer;
#endif
                }
                if (!chosen.empty()) {
                    std::string error;
                    if (acp::image_block(chosen, error)) agent.pendingImages.push_back(chosen);
                    else agent.transcript.error(error);
                }
            }
        }
        agent_panel::poll(entity.get<RepoComponent>(), agent);
    }
};

}  // namespace ecs
