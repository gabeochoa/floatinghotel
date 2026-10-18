#include "test_framework.h"
#include "../../src/lsp/json_rpc.h"
#include "../../src/lsp/lsp_client.h"
#include "../../src/lsp/semantic.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(framing_survives_any_chunking_and_rejects_bad_headers) {
    const std::string stream = json_rpc::frame("{\"a\":1}") + json_rpc::frame("{\"b\":\"\xce\xbb\"}");
    for (size_t chunk = 1; chunk <= stream.size(); ++chunk) {
        json_rpc::Decoder decoder;
        std::vector<std::string> bodies;
        for (size_t i = 0; i < stream.size(); i += chunk) {
            decoder.feed(std::string_view(stream).substr(i, chunk));
            while (auto body = decoder.next()) bodies.push_back(*body);
        }
        ASSERT_EQ(bodies.size(), 2u);
        ASSERT_EQ(bodies[1], std::string("{\"b\":\"\xce\xbb\"}"));
    }
    json_rpc::Decoder bad;
    bad.feed("Content-Length: x1\r\n\r\n{}");
    ASSERT_FALSE(bad.next().has_value());
    ASSERT_FALSE(bad.error().empty());
    json_rpc::Decoder huge;
    huge.feed("Content-Length: 999999999999\r\n\r\n");
    ASSERT_FALSE(huge.next().has_value());
    ASSERT_FALSE(huge.error().empty());
}

TEST(columns_convert_between_code_points_and_utf16_units) {
    const std::string line = "a\xce\xbb\xf0\x9f\x99\x82z";  // a, lambda (2 bytes), emoji (4 bytes, 2 units), z
    ASSERT_EQ(lsp::utf16_from_column(line, 1), 0);
    ASSERT_EQ(lsp::utf16_from_column(line, 3), 2);
    ASSERT_EQ(lsp::utf16_from_column(line, 4), 4);
    ASSERT_EQ(lsp::column_from_utf16(line, 4), 4);
    ASSERT_EQ(lsp::column_from_utf16(line, 2), 3);
    ASSERT_EQ(lsp::path_from_uri(lsp::uri_from_path("/tmp/a b/λ.cpp")), std::string("/tmp/a b/λ.cpp"));
    ASSERT_EQ(lsp::uri_from_path("/tmp/a b"), std::string("file:///tmp/a%20b"));
}

TEST(clangd_answers_definition_references_hover_and_incoming_calls) {
    if (access("/usr/bin/clangd", X_OK) != 0) { printf("  (clangd not installed; skipped)\n"); return; }
    char pattern[] = "/tmp/fh-lsp.XXXXXX";
    // Canonical: /tmp is a symlink, and a server reports both spellings of a
    // file it reached two ways (the semantic layer canonicalizes and dedupes).
    const std::filesystem::path root = std::filesystem::canonical(mkdtemp(pattern));
    const auto file = root / "a.cpp";
    { std::ofstream(file) << "int add(int a, int b) { return a + b; }\n"
                             "int twice(int x) { return add(x, x); }\n"
                             "int main() { return twice(2) + add(1, 2); }\n"; }
    { std::ofstream(root / "compile_commands.json") << "[{\"directory\":\"" << root.string() << "\",\"file\":\"a.cpp\","
                                                       "\"arguments\":[\"clang++\",\"-std=c++17\",\"-c\",\"a.cpp\"]}]"; }
    std::string error;
    auto client = lsp::Client::start({"/usr/bin/clangd"}, root.string(), error);
    ASSERT_TRUE(client != nullptr);
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const auto uri = lsp::uri_from_path(file.string());
    client->notify("textDocument/didOpen", {{"textDocument", {{"uri", uri}, {"languageId", "cpp"}, {"version", 1}, {"text", text}}}});
    auto at = [&](int line, int character) {
        return nlohmann::json{{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", character}}}};
    };
    auto get = [](lsp::Client::Pending p) { p.result.wait_for(std::chrono::seconds(30)); return p.result.get(); };
    auto definition = get(client->request("textDocument/definition", at(1, 26)));
    ASSERT_TRUE(definition.contains("result"));
    auto targets = definition["result"].is_array() ? definition["result"] : nlohmann::json::array({definition["result"]});
    ASSERT_FALSE(targets.empty());
    ASSERT_EQ(lsp::path_from_uri(targets[0]["uri"]), std::filesystem::canonical(file).string());
    ASSERT_EQ(targets[0]["range"]["start"]["line"].get<int>(), 0);
    auto params = at(0, 4);
    params["context"] = {{"includeDeclaration", true}};
    auto references = get(client->request("textDocument/references", params));
    ASSERT_EQ(references["result"].size(), 3u);
    auto hover = get(client->request("textDocument/hover", at(0, 4)));
    ASSERT_TRUE(hover["result"]["contents"].dump().find("int add(int a, int b)") != std::string::npos);
    auto prepared = get(client->request("textDocument/prepareCallHierarchy", at(0, 4)));
    ASSERT_EQ(prepared["result"].size(), 1u);
    auto incoming = get(client->request("callHierarchy/incomingCalls", {{"item", prepared["result"][0]}}));
    std::vector<std::string> callers;
    for (const auto& call : incoming["result"]) callers.push_back(call["from"]["name"]);
    std::sort(callers.begin(), callers.end());
    ASSERT_EQ(callers, (std::vector<std::string>{"main", "twice"}));
    // A cancelled request settles at once; the server keeps serving.
    auto slow = client->request("textDocument/references", params);
    client->cancel(slow.id);
    ASSERT_TRUE(slow.result.get().contains("error"));
    ASSERT_TRUE(get(client->request("textDocument/hover", at(0, 4))).contains("result"));
    client.reset();
    std::filesystem::remove_all(root);
}


TEST(semantic_sessions_return_repository_relative_deduplicated_locations) {
    if (access("/usr/bin/clangd", X_OK) != 0) { printf("  (clangd not installed; skipped)\n"); return; }
    // Deliberately the /tmp spelling: the server sees /private/tmp too.
    char pattern[] = "/tmp/fh-semantic.XXXXXX";
    const std::string root = mkdtemp(pattern);
    std::filesystem::create_directories(root + "/src");
    const std::string text = "int add(int a, int b) { return a + b; }\n"
                             "int twice(int x) { return add(x, x); }\n"
                             "int main() { return twice(2) + add(1, 2); }\n";
    { std::ofstream(root + "/src/a.cpp") << text; }
    { std::ofstream(root + "/compile_commands.json") << "[{\"directory\":\"" << root << "/src\",\"file\":\"a.cpp\","
                                                       "\"arguments\":[\"clang++\",\"-std=c++17\",\"-c\",\"a.cpp\"]}]"; }
    semantic::Session session(root);
    auto definition = session.run(semantic::Kind::Definition, "src/a.cpp", text, 2, 27).get();
    ASSERT_TRUE(definition.error.empty());
    ASSERT_EQ(definition.locations.size(), 1u);
    ASSERT_EQ(definition.locations[0].path, std::string("src/a.cpp"));
    ASSERT_TRUE(definition.locations[0].inside);
    ASSERT_EQ(definition.locations[0].line, 1);
    ASSERT_EQ(definition.locations[0].column, 5);
    auto references = session.run(semantic::Kind::References, "src/a.cpp", text, 1, 5).get();
    ASSERT_EQ(references.locations.size(), 3u);
    auto hover = session.run(semantic::Kind::Hover, "src/a.cpp", text, 1, 5).get();
    ASSERT_TRUE(hover.hover.find("int add(int a, int b)") != std::string::npos);
    auto callers = session.run(semantic::Kind::Incoming, "src/a.cpp", text, 1, 5).get();
    std::vector<std::string> labels;
    for (const auto& location : callers.locations) labels.push_back(location.label);
    std::sort(labels.begin(), labels.end());
    ASSERT_EQ(labels, (std::vector<std::string>{"main", "twice"}));
    auto callees = session.run(semantic::Kind::Outgoing, "src/a.cpp", text, 3, 5).get();
    ASSERT_EQ(callees.locations.size(), 2u);
    // An edited buffer is what the server answers about.
    const std::string edited = "int add(int a, int b) { return a + b; }\n\nint twice(int x) { return add(x, x); }\n";
    auto moved = session.run(semantic::Kind::References, "src/a.cpp", edited, 1, 5).get();
    ASSERT_EQ(moved.locations.size(), 2u);
    ASSERT_EQ(moved.locations[1].line, 3);
    // Dropping the task cancels it; the session keeps working.
    { auto dropped = session.run(semantic::Kind::References, "src/a.cpp", edited, 1, 5); }
    ASSERT_TRUE(session.run(semantic::Kind::Hover, "src/a.cpp", edited, 1, 5).get().error.empty());
    std::string missing;
    ASSERT_FALSE(semantic::server_for("lib.rs", &missing).has_value() && access("/usr/bin/rust-analyzer", X_OK) != 0);
    auto none = session.run(semantic::Kind::Definition, "notes.txt", "x", 1, 1).get();
    ASSERT_FALSE(none.error.empty());
    std::filesystem::remove_all(root);
}

int main() { RUN_ALL_TESTS(); }
