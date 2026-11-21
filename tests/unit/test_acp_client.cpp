#include "test_framework.h"
#include "../../src/acp/acp_client.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

TEST(command_splitting_handles_quotes) {
    ASSERT_EQ(acp::split_command("muse acp --provider echo"),
              (std::vector<std::string>{"muse", "acp", "--provider", "echo"}));
    ASSERT_EQ(acp::split_command("'/opt/my agent/acp' --flag=\"a b\""),
              (std::vector<std::string>{"/opt/my agent/acp", "--flag=a b"}));
    ASSERT_TRUE(acp::split_command("   ").empty());
    ASSERT_TRUE(acp::on_path("ls"));
    ASSERT_FALSE(acp::on_path("definitely-not-a-real-program-fh"));
}

TEST(line_decoder_survives_chunking) {
    const std::string stream = "{\"a\":1}\r\n{\"b\":2}\n\ntail";
    for (size_t chunk = 1; chunk <= stream.size(); ++chunk) {
        acp::LineDecoder decoder;
        std::vector<std::string> lines;
        for (size_t i = 0; i < stream.size(); i += chunk) {
            decoder.feed(std::string_view(stream).substr(i, chunk));
            while (auto line = decoder.next()) lines.push_back(*line);
        }
        ASSERT_EQ(lines.size(), 3u);
        ASSERT_EQ(lines[0], std::string("{\"a\":1}"));
        ASSERT_EQ(lines[1], std::string("{\"b\":2}"));
        ASSERT_TRUE(lines[2].empty());
    }
}

TEST(fs_root_confines_reads_and_writes) {
    char pattern[] = "/tmp/fh-acp-fs.XXXXXX";
    const std::filesystem::path root = mkdtemp(pattern);
    const auto fs = acp::FsRoot::make(root);
    std::string error;
    ASSERT_TRUE(acp::write_text_file(fs, "sub/a.txt", "one\ntwo\nthree\n", error));
    auto all = acp::read_text_file(fs, (root / "sub/a.txt").string(), 0, 0, error);
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(*all, std::string("one\ntwo\nthree\n"));
    auto slice = acp::read_text_file(fs, "sub/a.txt", 2, 1, error);
    ASSERT_TRUE(slice.has_value());
    ASSERT_EQ(*slice, std::string("two\n"));
    ASSERT_FALSE(acp::write_text_file(fs, "../escape.txt", "no", error));
    ASSERT_FALSE(acp::read_text_file(fs, "/etc/passwd", 0, 0, error).has_value());
    ASSERT_FALSE(error.empty());
    // A symlink inside the repo pointing out must not smuggle a write.
    std::error_code ec;
    std::filesystem::create_directory_symlink("/tmp", root / "link", ec);
    if (!ec) ASSERT_FALSE(acp::write_text_file(fs, "link/escape.txt", "no", error));
    std::filesystem::remove_all(root);
}

TEST(transcript_merges_chunks_and_describes_tool_diffs) {
    acp::Transcript transcript;
    transcript.apply_update({{"sessionUpdate", "agent_message_chunk"},
                             {"content", {{"type", "text"}, {"text", "Hello "}}}});
    transcript.apply_update({{"sessionUpdate", "agent_message_chunk"},
                             {"content", {{"type", "text"}, {"text", "world"}}}});
    transcript.apply_update({{"sessionUpdate", "tool_call"},
                             {"toolCallId", "t1"}, {"title", "Edit"}, {"kind", "edit"},
                             {"status", "in_progress"}});
    transcript.apply_update({{"sessionUpdate", "tool_call_update"},
                             {"toolCallId", "t1"}, {"status", "completed"},
                             {"content", {{{"type", "diff"}, {"path", "/r/a.cpp"},
                                           {"oldText", "a\n"}, {"newText", "b\nc\n"}}}}});
    transcript.apply_update({{"sessionUpdate", "agent_message_chunk"},
                             {"content", {{"type", "text"}, {"text", "done"}}}});
    ASSERT_EQ(transcript.entries.size(), 3u);
    // The same update resent whole must replace, not duplicate.
    transcript.apply_update({{"sessionUpdate", "tool_call_update"},
                             {"toolCallId", "t1"}, {"status", "completed"},
                             {"content", {{{"type", "diff"}, {"path", "/r/a.cpp"},
                                           {"oldText", "a\n"}, {"newText", "b\nc\n"}}}}});
    ASSERT_EQ(transcript.entries[0].text, std::string("Hello world"));
    ASSERT_EQ(transcript.entries[1].status, std::string("completed"));
    const std::string& toolText = transcript.entries[1].text;
    ASSERT_TRUE(toolText.find("/r/a.cpp") != std::string::npos);
    ASSERT_TRUE(toolText.find("/r/a.cpp", toolText.find("/r/a.cpp") + 1) == std::string::npos);
    ASSERT_EQ(transcript.entries[1].locationPath, std::string("/r/a.cpp"));
    ASSERT_EQ(transcript.entries[2].text, std::string("done"));
    ASSERT_TRUE(transcript.plain().find("Hello world") != std::string::npos);
}

TEST(phase_name_covers_all_phases) {
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Unstarted)), std::string("Unstarted"));
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Starting)), std::string("Starting"));
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Ready)), std::string("Ready"));
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Prompting)), std::string("Prompting"));
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Dead)), std::string("Dead"));
    ASSERT_EQ(std::string(acp::phase_name(acp::Phase::Failed)), std::string("Failed"));
}

TEST(default_registry_lists_muse_claude_codex_with_cli_env) {
    const auto agents = acp::default_agents();
    ASSERT_EQ(agents.size(), 3u);
    ASSERT_EQ(agents[0].id, std::string("muse"));
    ASSERT_EQ(agents[1].id, std::string("claude"));
    ASSERT_EQ(agents[2].id, std::string("codex"));
    ASSERT_TRUE(acp::find_agent(agents, "codex") != nullptr);
    ASSERT_TRUE(acp::find_agent(agents, "nope") == nullptr);
    ASSERT_FALSE(acp::find_on_path("ls").empty());
    // With a `claude` CLI installed (and no user-set override), the
    // adapter is pointed at it so it inherits that binary's auth setup.
    if (!acp::find_on_path("claude").empty() && !std::getenv("CLAUDE_CODE_EXECUTABLE")) {
        bool pointed = false;
        for (const auto& [key, value] : agents[1].env)
            pointed |= key == "CLAUDE_CODE_EXECUTABLE" && value == acp::find_on_path("claude");
        ASSERT_TRUE(pointed);
    }
}

TEST(fake_agent_end_to_end_fs_and_permission) {
    if (access("/usr/bin/python3", X_OK) != 0 && !acp::on_path("python3")) {
        printf("  (python3 not installed; skipped)\n");
        return;
    }
    char pattern[] = "/tmp/fh-acp-e2e.XXXXXX";
    const std::filesystem::path root = std::filesystem::canonical(mkdtemp(pattern));
    { std::ofstream(root / "input.txt") << "hello acp\n"; }
    const auto fs = acp::FsRoot::make(root);

    // Absolute: the agent process runs with cwd = the temp repo.
    const std::string script = (std::filesystem::current_path() / "tests/fake_acp_agent.py").string();
    acp::AgentDef fake{"fake", "Fake", "", {"python3", script}, true};
    std::string error;
    auto client = acp::Client::spawn(fake, root.string(), error, std::chrono::seconds(30));
    ASSERT_TRUE(client != nullptr);

    acp::Transcript transcript;
    bool done = false, sawPermission = false;
    std::string stop;
    client->on_event = [&](const acp::Event& event) {
        switch (event.kind) {
            case acp::Event::Kind::Update: transcript.apply_update(event.update); break;
            case acp::Event::Kind::FsRead: {
                std::string fsError;
                auto content = acp::read_text_file(fs, event.params.value("path", ""),
                                                   event.params.value("line", 0),
                                                   event.params.value("limit", 0), fsError);
                if (content) client->answer_fs_read(event.id, *content);
                else client->answer_error(event.id, fsError);
                break;
            }
            case acp::Event::Kind::FsWrite: {
                std::string fsError;
                if (acp::write_text_file(fs, event.params.value("path", ""),
                                         event.params.value("content", ""), fsError))
                    client->answer_fs_write(event.id);
                else client->answer_error(event.id, fsError);
                break;
            }
            case acp::Event::Kind::Permission:
                sawPermission = true;
                client->answer_permission(event.id, "allow");
                break;
            case acp::Event::Kind::PromptDone:
                done = true;
                stop = event.text;
                ASSERT_FALSE(event.failed);
                break;
            case acp::Event::Kind::Log:
                fprintf(stderr, "  [agent stderr] %s\n", event.text.c_str());
                break;
            case acp::Event::Kind::Exited: break;
        }
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool prompted = false;
    while (std::chrono::steady_clock::now() < deadline) {
        client->pump();
        if (!prompted && client->phase() == acp::Phase::Ready) {
            ASSERT_EQ(client->session_id(), std::string("fake-1"));
            ASSERT_EQ(client->modes().size(), 2u);
            ASSERT_EQ(client->current_mode(), std::string("ask"));
            ASSERT_TRUE(client->embedded_context());
            prompted = client->prompt({{{"type", "text"}, {"text", "uppercase it"}}});
            ASSERT_TRUE(prompted);
        }
        if (done) break;
        if (client->phase() == acp::Phase::Dead || client->phase() == acp::Phase::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(done);
    ASSERT_EQ(stop, std::string("end_turn"));
    ASSERT_TRUE(sawPermission);
    ASSERT_TRUE(transcript.plain().find("Hello world") != std::string::npos);
    ASSERT_TRUE(transcript.plain().find("TERMOK") != std::string::npos);
    std::ifstream written(root / "written.txt");
    ASSERT_TRUE(written.good());
    const std::string text((std::istreambuf_iterator<char>(written)), {});
    ASSERT_EQ(text, std::string("HELLO ACP\n"));
    client.reset();
    std::filesystem::remove_all(root);
}

TEST(fake_agent_session_list_and_load_replays) {
    if (!acp::on_path("python3")) { printf("  (python3 not installed; skipped)\n"); return; }
    char pattern[] = "/tmp/fh-acp-load.XXXXXX";
    const std::filesystem::path root = std::filesystem::canonical(mkdtemp(pattern));
    const std::string script = (std::filesystem::current_path() / "tests/fake_acp_agent.py").string();
    acp::AgentDef fake{"fake", "Fake", "", {"python3", script}, true};
    std::string error;
    auto client = acp::Client::spawn(fake, root.string(), error, std::chrono::seconds(30));
    ASSERT_TRUE(client != nullptr);
    acp::Transcript transcript;
    client->on_event = [&](const acp::Event& event) {
        if (event.kind == acp::Event::Kind::Update) transcript.apply_update(event.update);
        if (event.kind == acp::Event::Kind::Log)
            fprintf(stderr, "  [agent stderr] %s\n", event.text.c_str());
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto pump_until = [&](auto predicate) {
        while (std::chrono::steady_clock::now() < deadline && !predicate()) {
            client->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return predicate();
    };
    ASSERT_TRUE(pump_until([&] { return client->phase() == acp::Phase::Ready; }));
    ASSERT_TRUE(client->can_list_sessions());
    ASSERT_TRUE(client->can_load_sessions());
    ASSERT_TRUE(client->list_sessions());
    ASSERT_TRUE(pump_until([&] { return client->sessions().size() == 1u; }));
    ASSERT_EQ(client->sessions()[0].id, std::string("fake-old"));
    ASSERT_EQ(client->sessions()[0].title, std::string("Old chat"));
    transcript.clear();
    ASSERT_TRUE(client->load_session("fake-old"));
    ASSERT_TRUE(pump_until([&] { return client->phase() == acp::Phase::Ready; }));
    // The load result carries no sessionId; the client keeps the loaded one.
    ASSERT_EQ(client->session_id(), std::string("fake-old"));
    ASSERT_TRUE(transcript.plain().find("earlier question") != std::string::npos);
    ASSERT_TRUE(transcript.plain().find("earlier answer") != std::string::npos);
    client.reset();
    std::filesystem::remove_all(root);
}

int main() { RUN_ALL_TESTS(); }
