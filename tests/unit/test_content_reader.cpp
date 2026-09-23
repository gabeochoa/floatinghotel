#include "src/git/diff_syntax.h"
#include "src/ui/diff_syntax.h"
#include "test_framework.h"
#include "../../src/util/navigation.h"
#include "../../src/git/content_reader.h"
#include "../../src/git/git_parser.h"
#include "../../src/util/file_page.h"
#include "../../src/git/blob_page_cache.h"
#include "../../src/git/repository_lock.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(complete_file_parser_preserves_lines_and_endings) {
    auto file = git::parse_complete_file("file.cpp", "first\r\n\nlast");
    ASSERT_TRUE(file.isFullContent);
    ASSERT_FALSE(file.isBinary);
    ASSERT_EQ(file.hunks.size(), 1u);
    ASSERT_EQ(file.hunks[0].lines, (std::vector<std::string>{" first\r", " ", " last"}));
    ASSERT_EQ(file.hunks[0].newCount, 3);
    ASSERT_TRUE(file.hunks[0].noNewline.contains(2));
    ASSERT_TRUE(git::parse_complete_file("empty", "").hunks[0].lines.empty());
}

TEST(binary_file_keeps_its_raw_content) {
    ASSERT_TRUE(git::parse_complete_file("file.bin", std::string("a\0b", 3)).isBinary);
}

TEST(file_reads_report_errors_without_throwing) {
    auto result = git::read_file({"/tmp", "fh-path-that-does-not-exist/absent", ""});
    ASSERT_FALSE(result.error.empty());
    ASSERT_TRUE(result.raw.empty());
}

TEST(async_file_read_returns_owned_content) {
    char directory[] = "/tmp/fh-content-test.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    const auto file = std::filesystem::path(path) / "source.cpp";
    { std::ofstream output(file); output << "int answer = 42;\n"; }
    auto pending = git::read_file_async({path, "source.cpp", ""});
    ASSERT_EQ(pending.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto result = pending.get();
    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.raw, "int answer = 42;\n");
    ASSERT_EQ(result.diff.hunks[0].lines[0], " int answer = 42;");
    std::filesystem::remove(file);
    std::filesystem::remove(path);
}

TEST(markdown_preview_text_uses_worker_decoding_without_retaining_other_sources) {
    char directory[] = "/tmp/fh-markdown-decoding.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    for (const auto* name : {"notes.md", "source.txt"}) {
        std::ofstream output(std::filesystem::path(path) / name, std::ios::binary);
        output.write("\xff\xfe#\0 \0T\0\n\0", 10);
    }
    auto markdown = git::read_file({path, "notes.md", ""});
    ASSERT_TRUE(markdown.error.empty());
    ASSERT_EQ(markdown.decodedText, "# T\n");
    ASSERT_EQ(markdown.diff.hunks.front().lines.front(), " # T");
    ASSERT_TRUE(git::read_file({path, "source.txt", ""}).decodedText.empty());
    std::filesystem::remove_all(path);
}

TEST(replacing_a_task_requests_stop_without_waiting_for_completion) {
    std::promise<void> stopped;
    auto observed = stopped.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto pending = async_work::launch([&stopped, gate](std::stop_token stop) {
        while (!stop.stop_requested()) std::this_thread::yield();
        stopped.set_value();
        gate.wait();
        return 1;
    });
    pending = {};
    auto ready = observed.wait_for(std::chrono::seconds(2));
    release.set_value();
    ASSERT_EQ(ready, std::future_status::ready);
}

TEST(cancelled_tokens_do_not_cancel_accepted_git_writes) {
    char directory[] = "/tmp/fh-write-test.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    std::stop_source stop;
    stop.request_stop();
    ASSERT_TRUE(git::git_run(path, {"config", "test.completed", "yes"}, stop.get_token()).success());
    ASSERT_EQ(git::git_run(path, {"config", "--get", "test.completed"}).stdout_str(), "yes\n");
    ASSERT_TRUE(git::git_run(path, {"status"}, stop.get_token()).raw.cancelled);
}

TEST(file_pages_bound_lines_navigate_both_directions_and_load_distant_targets) {
    char directory[] = "/tmp/fh-page-lines.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    { std::ofstream file(std::filesystem::path(path) / "large.txt");
      for (int i = 1; i <= 15000; ++i) file << "line " << i << '\n'; }
    auto first = git::read_file({path, "large.txt", ""});
    ASSERT_TRUE(first.error.empty());
    ASSERT_TRUE(first.raw.size() <= file_page::byteLimit);
    ASSERT_EQ(first.diff.hunks[0].newCount, file_page::lineLimit);
    ASSERT_EQ(first.page.next.line, 4097);
    ASSERT_TRUE(first.diff.isPartialContent);
    git::FileRequest request{path, "large.txt", ""};
    request.page = {ecs::FilePageRequest::Action::Next, first.page.next, 0, first.page.sourceIdentity};
    request.detectedEncoding = first.page.encoding;
    auto second = git::read_file(request);
    ASSERT_TRUE(second.error.empty());
    ASSERT_EQ(second.diff.hunks[0].newStart, 4097);
    ASSERT_EQ(second.diff.hunks[0].lines[0], " line 4097");
    request.page = {ecs::FilePageRequest::Action::Previous, second.page.begin, 0, second.page.sourceIdentity};
    ASSERT_EQ(git::read_file(request).raw, first.raw);
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, 14000};
    auto target = git::read_file(request);
    ASSERT_EQ(target.diff.hunks[0].newStart, 14000);
    ASSERT_EQ(target.diff.hunks[0].lines[0], " line 14000");
    request.page = {ecs::FilePageRequest::Action::Previous, target.page.begin, 0, target.page.sourceIdentity};
    auto beforeTarget = git::read_file(request);
    ASSERT_EQ(beforeTarget.page.next.offset, target.page.begin.offset);
    ASSERT_EQ(beforeTarget.diff.hunks[0].lines.back(), " line 13999");
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, 16000};
    ASSERT_FALSE(git::read_file(request).error.empty());
    { std::ofstream file(std::filesystem::path(path) / "large.txt"); file << "changed\n"; }
    request.page = {ecs::FilePageRequest::Action::Next, first.page.next, 0, first.page.sourceIdentity};
    ASSERT_FALSE(git::read_file(request).error.empty());
    std::filesystem::remove_all(path);
}

TEST(target_pages_include_bounded_leading_context_without_losing_the_target) {
    ecs::FilePageRequest request{ecs::FilePageRequest::Action::TargetLine, {}, 4, {}, 2};
    file_page::Collector shortFile(request, "utf8", "");
    shortFile.consume("one\ntwo\nthree\ntarget\nfive\n");
    shortFile.finish();
    ASSERT_EQ(shortFile.begin.line, 2);
    ASSERT_EQ(shortFile.raw, "two\nthree\ntarget\nfive\n");
    ASSERT_TRUE(shortFile.error.empty());
    std::string longPrefix = "one\n" + std::string(file_page::byteLimit * 3, 'x') + "\nthree\ntarget\nfive\n";
    file_page::Collector bounded(request, "utf8", "");
    for (size_t offset = 0; offset < longPrefix.size(); offset += 4096)
        if (!bounded.consume(std::string_view(longPrefix).substr(offset, 4096))) break;
    bounded.finish();
    ASSERT_TRUE(bounded.error.empty());
    ASSERT_TRUE(bounded.raw.size() <= file_page::byteLimit);
    ASSERT_TRUE(bounded.raw.ends_with("\nthree\ntarget\nfive\n"));
    request.targetLine = 6;
    file_page::Collector absent(request, "utf8", "");
    absent.consume("one\ntwo\nthree\nfour\nfive\n");
    absent.finish();
    ASSERT_FALSE(absent.error.empty());
    git::FileRequest plain{"repo", "a.cpp", "HEAD"};
    plain.page = request;
    auto contextKey = git::blob_page_key(plain, "blob");
    plain.page.leadingLines = 0;
    ASSERT_TRUE(contextKey != git::blob_page_key(plain, "blob"));
}

TEST(file_pages_keep_utf16_units_and_long_line_fragments_intact) {
    std::string bytes("\xff\xfe", 2);
    for (size_t i = 0; i < (file_page::byteLimit - 4) / 2; ++i) bytes += std::string("A\0", 2);
    bytes += std::string("\x3d\xd8\x00\xde\n\0Z\0", 8);
    file_page::Collector first({}, "auto", "");
    for (size_t i = 0; i < bytes.size(); i += 3)
        if (!first.consume(std::string_view(bytes).substr(i, 3))) break;
    first.finish();
    ASSERT_EQ(first.encoding, "utf16le");
    ASSERT_TRUE(first.raw.size() <= file_page::byteLimit);
    ASSERT_EQ(first.raw.size() % 2, 0u);
    ASSERT_TRUE(first.next.continuation);
    file_page::Collector second({ecs::FilePageRequest::Action::Next, first.next}, "auto", first.encoding);
    second.consume(bytes);
    second.finish();
    ASSERT_EQ(first.raw + second.raw, bytes);
    ASSERT_FALSE(text_decode::decode(second.raw, second.encoding).malformed);
    ASSERT_EQ(second.begin.line, 1);
    ASSERT_EQ(second.next.line, 2);
}

TEST(file_pages_do_not_split_utf8_codepoints_at_the_byte_budget) {
    auto bytes = std::string(file_page::byteLimit - 1, 'x') + "\xf0\x9f\x98\x80\n";
    file_page::Collector first({}, "utf8", "");
    first.consume(bytes);
    first.finish();
    ASSERT_EQ(first.raw.size(), file_page::byteLimit - 1);
    file_page::Collector second({ecs::FilePageRequest::Action::Next, first.next}, "utf8", "");
    second.consume(bytes);
    second.finish();
    ASSERT_EQ(first.raw + second.raw, bytes);
    ASSERT_FALSE(text_decode::decode(second.raw, "utf8").malformed);
    ASSERT_EQ(file_page::decode("\xef\xbb\xbftext", "utf8", 10).text, "\xef\xbb\xbftext");
    ASSERT_EQ(file_page::decode(std::string("\xff\xfeZ\0", 4), "utf16le", 10).text, "\xef\xbb\xbfZ");
}

TEST(historical_reads_are_bounded_and_index_is_resolved_fresh) {
    char directory[] = "/tmp/fh-page-history.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    { std::ofstream file(std::filesystem::path(path) / "large.txt"); file << std::string(file_page::byteLimit * 4, 'x'); }
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    auto first = git::read_file({path, "large.txt", "INDEX"});
    ASSERT_TRUE(first.error.empty());
    ASSERT_EQ(first.raw.size(), file_page::byteLimit);
    ASSERT_EQ(first.page.totalBytes, file_page::byteLimit * 4);
    ASSERT_FALSE(first.page.blob.empty());
    { std::ofstream file(std::filesystem::path(path) / "large.txt"); file << "new index\n"; }
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    auto next = git::read_file({path, "large.txt", "INDEX"});
    ASSERT_TRUE(first.page.blob != next.page.blob);
    ASSERT_EQ(next.raw, "new index\n");
    std::filesystem::remove_all(path);
}

TEST(later_utf16_pages_preserve_binary_nuls) {
    ASSERT_TRUE(file_page::decode(std::string(4, '\0'), "utf16le", 256).binary);
    ASSERT_TRUE(file_page::decode(std::string(4, '\0'), "utf16be", 256).binary);
}

TEST(blob_page_cache_bounds_owned_bytes_and_returns_independent_copies) {
    git::BlobPage sample{std::string(1024, 'x'), {}};
    git::BlobPageCache probe;
    ASSERT_TRUE(probe.put("a", sample));
    auto budget = probe.bytes() * 2;
    git::BlobPageCache cache(budget);
    ASSERT_TRUE(cache.put("a", sample));
    ASSERT_TRUE(cache.put("b", sample));
    auto owned = cache.get("a");
    ASSERT_TRUE(owned.has_value());
    owned->raw[0] = 'z';
    ASSERT_EQ(cache.get("a")->raw[0], 'x');
    ASSERT_TRUE(cache.put("c", sample));
    ASSERT_FALSE(cache.get("b").has_value());
    ASSERT_TRUE(cache.get("a").has_value());
    ASSERT_TRUE(cache.bytes() <= budget);
    sample.raw.assign(budget * 3, 'x');
    ASSERT_FALSE(cache.put("large", sample));
    ASSERT_TRUE(cache.bytes() <= budget);
    ASSERT_FALSE(cache.put("", {}));
}

TEST(blob_page_keys_require_immutable_sources_and_distinguish_page_and_encoding_options) {
    git::FileRequest request{"repo", "path", ""};
    ASSERT_TRUE(git::blob_page_key(request, "object").empty());
    request.revision = "INDEX";
    auto key = git::blob_page_key(request, "object");
    ASSERT_FALSE(key.empty());
    for (int i = 0; i < 8; ++i) {
        auto changed = request;
        std::string object = "object";
        if (i == 0) object = "different-object";
        if (i == 1) changed.page.action = ecs::FilePageRequest::Action::Next;
        if (i == 2) changed.page.cursor.offset = 100;
        if (i == 3) changed.page.cursor.line = 50;
        if (i == 4) changed.page.cursor.continuation = true;
        if (i == 5) changed.page.targetLine = 9000;
        if (i == 6) changed.encoding = "utf16le";
        if (i == 7) changed.detectedEncoding = "utf16be";
        ASSERT_TRUE(git::blob_page_key(changed, object) != key);
    }
}

TEST(cached_blob_pages_do_not_leak_path_or_mode_and_never_cache_working_tree_bytes) {
    char directory[] = "/tmp/fh-blob-page-cache.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    for (const auto* name : {"one.txt", "two.sh"}) {
        std::ofstream file(std::filesystem::path(path) / name); file << "shared content\n";
    }
    std::filesystem::permissions(std::filesystem::path(path) / "two.sh", std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    auto one = git::read_file({path, "one.txt", "INDEX"});
    auto two = git::read_file({path, "two.sh", "INDEX"});
    ASSERT_TRUE(one.error.empty());
    ASSERT_TRUE(two.error.empty());
    ASSERT_EQ(one.page.blob, two.page.blob);
    ASSERT_EQ(two.diff.filePath, "two.sh");
    ASSERT_EQ(two.diff.newMode, "100755");
    ASSERT_EQ(one.diff.newMode, "100644");
    ASSERT_EQ(git::read_file({path, "one.txt", ""}).raw, "shared content\n");
    { std::ofstream file(std::filesystem::path(path) / "one.txt"); file << "working changed\n"; }
    ASSERT_EQ(git::read_file({path, "one.txt", ""}).raw, "working changed\n");
    ASSERT_EQ(git::read_file({path, "one.txt", "INDEX"}).raw, "shared content\n");
    std::filesystem::remove_all(path);
}

TEST(intentional_page_capture_is_logged_as_success_without_hiding_its_stop_result) {
    char directory[] = "/tmp/fh-capture-log.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    { std::ofstream file(std::filesystem::path(path) / "large.txt"); file << std::string(100000, 'x'); }
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    bool loggedSuccess = false;
    std::string loggedOutput;
    git::set_log_callback([&](const auto&, const auto& output, const auto&, bool success) {
        loggedSuccess = success;
        loggedOutput = output;
    });
    auto result = git::git_run(path, {"cat-file", "blob", ":large.txt"}, {},
                               [](std::string_view) { return false; });
    git::set_log_callback({});
    ASSERT_TRUE(result.raw.outputStopped);
    ASSERT_FALSE(result.success());
    ASSERT_TRUE(loggedSuccess);
    ASSERT_TRUE(loggedOutput.find("requested output limit") != std::string::npos);
    std::filesystem::remove_all(path);
}

TEST(hidden_pending_file_read_is_cancelled_and_will_retry_when_reopened) {
    ecs::RepoComponent repo;
    std::promise<ecs::FullFileContent> promise;
    std::stop_source source;
    repo.fullFileFuture = {promise.get_future(), source};
    repo.fullFileCacheKey = "pending request";
    ecs::cancel_hidden_file_read(repo);
    ASSERT_TRUE(source.stop_requested());
    ASSERT_FALSE(repo.fullFileFuture.valid());
    ASSERT_TRUE(repo.fullFileCacheKey.empty());
}

TEST(hidden_loaded_file_retains_its_cache_and_visible_pending_read_keeps_running) {
    ecs::RepoComponent repo;
    repo.fullFileCacheKey = "loaded request";
    repo.sourceWindow.raw = "loaded source";
    ecs::cancel_hidden_file_read(repo);
    ASSERT_EQ(repo.fullFileCacheKey, "loaded request");
    ASSERT_EQ(repo.sourceWindow.raw, "loaded source");
    std::promise<ecs::FullFileContent> promise;
    std::stop_source source;
    navigation::open(repo, reading::source("visible.cpp"));
    repo.fullFileFuture = {promise.get_future(), source};
    ecs::cancel_hidden_file_read(repo);
    ASSERT_FALSE(source.stop_requested());
    ASSERT_TRUE(repo.fullFileFuture.valid());
}

TEST(leaving_file_view_releases_a_worker_waiting_for_the_repository_lock) {
    char directory[] = "/tmp/fh-hidden-file-read.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    auto mutex = git::repository_mutex(path);
    std::unique_lock held(*mutex);
    async_work::Executor pool(1, 4);
    std::promise<void> entered;
    ecs::RepoComponent repo;
    repo.fullFileCacheKey = "pending HEAD file";
    repo.fullFileFuture = async_work::launch_on(pool, [&entered, path](std::stop_token stop) {
        entered.set_value();
        return git::read_file({path, "file.cpp", "HEAD"}, stop);
    });
    ASSERT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ecs::cancel_hidden_file_read(repo);
    auto barrier = async_work::launch_on(pool, [](std::stop_token) { return true; });
    auto ready = barrier.wait_for(std::chrono::seconds(2));
    held.unlock();
    barrier.get();
    std::filesystem::remove_all(path);
    ASSERT_EQ(ready, std::future_status::ready);
    ASSERT_TRUE(repo.fullFileCacheKey.empty());
}

TEST(historical_navigation_pins_a_revision_even_after_the_branch_moves) {
    char directory[] = "/tmp/fh-pinned-navigation.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    ASSERT_TRUE(git::git_run(path, {"config", "user.name", "Navigation test"}).success());
    ASSERT_TRUE(git::git_run(path, {"config", "user.email", "navigation@example.invalid"}).success());
    ASSERT_TRUE(git::git_run(path, {"config", "commit.gpgsign", "false"}).success());
    auto file = std::filesystem::path(path) / "deleted.cpp";
    { std::ofstream out(file); out << "original\n"; }
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(path, {"commit", "-qm", "original"}).success());
    ecs::RepoComponent repo;
    repo.repoPath = path;
    navigation::open(repo, reading::source("deleted.cpp", "HEAD"));
    auto request = navigation::stamp(repo, "first");
    auto result = git::read_file({path, repo.fullFilePath(), repo.fullFileRevision()});
    ASSERT_TRUE(result.error.empty());
    ASSERT_TRUE(navigation::resolve_source(repo, request, result.resolvedRevision));
    ASSERT_TRUE(reading::is_object_id(repo.fullFileRevision()));
    ASSERT_TRUE(git::git_run(path, {"rm", "deleted.cpp"}).success());
    ASSERT_TRUE(git::git_run(path, {"commit", "-qm", "deleted"}).success());
    navigation::open(repo, reading::source("working.cpp"));
    navigation::step(repo, -1);
    auto pinned = git::read_file({path, repo.fullFilePath(), repo.fullFileRevision()});
    ASSERT_TRUE(pinned.error.empty());
    ASSERT_EQ(pinned.raw, "original\n");
    auto missing = git::read_file({path, "deleted.cpp", "HEAD"});
    ASSERT_FALSE(missing.error.empty());
    ASSERT_TRUE(missing.raw.empty());
    std::filesystem::remove_all(path);
}

TEST(untracked_review_preserves_text_and_bounds_large_previews) {
    char directory[] = "/tmp/fh-untracked-review.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    { std::ofstream out(std::filesystem::path(path) / "new.cpp"); out << "α\r\n\nlast"; }
    { std::ofstream out(std::filesystem::path(path) / "large.txt"); for (int i = 0; i < 6000; ++i) out << "line\n"; }
    auto result = git::read_untracked_review_files_async(path, {"new.cpp", "large.txt", "missing"}).get();
    ASSERT_EQ(result.files.size(), 3u);
    const auto& file = result.files[0];
    ASSERT_TRUE(file.isNew);
    ASSERT_FALSE(file.isFullContent);
    ASSERT_FALSE(file.isPartialContent);
    ASSERT_EQ(file.additions, 3);
    ASSERT_EQ(file.deletions, 0);
    ASSERT_EQ(file.hunks[0].oldCount, 0);
    ASSERT_EQ(file.hunks[0].lines, (std::vector<std::string>{"+α\r", "+", "+last"}));
    ASSERT_TRUE(file.hunks[0].noNewline.contains(2));
    ASSERT_TRUE(result.files[1].isPartialContent);
    ecs::ReviewComponent review;
    const auto& partial = result.files[1];
    review.reviewedFiles["wt\n" + partial.filePath] = ecs::diff_signature(partial);
    for (const auto& hunk : partial.hunks)
        review.approvedHunks.insert("wt\n" + ecs::ReviewComponent::hunk_key(partial.filePath, hunk));
    ASSERT_FALSE(ecs::file_reviewed(review, "wt", partial));
    ASSERT_FALSE(ecs::review_progress(review, "wt", std::vector<ecs::FileDiff>{partial}).can_approve());
    ASSERT_TRUE(result.files[1].additions <= 4096);
    ASSERT_TRUE(result.files[2].isPartialContent);
    ASSERT_FALSE(result.notice.empty());
    std::stop_source stopped;
    stopped.request_stop();
    ASSERT_TRUE(git::read_untracked_review_files(path, {"new.cpp"}, stopped.get_token()).files.empty());
    std::filesystem::remove_all(path);
}

TEST(untracked_review_includes_files_inside_new_directories) {
    char pattern[] = "/tmp/fh-untracked-nested.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    std::filesystem::create_directories(path / "new/dir");
    { std::ofstream file(path / "new/dir/新\nfile.cpp"); file << "new nested content\n"; }
    auto status = git::git_status(directory);
    ASSERT_TRUE(status.success());
    auto parsed = git::parse_status(status.stdout_str());
    ASSERT_EQ(parsed.untrackedFiles, (std::vector<std::string>{"new/dir/新\nfile.cpp"}));
    auto preview = git::read_untracked_review_files(directory, parsed.untrackedFiles);
    ASSERT_TRUE(preview.notice.empty());
    ASSERT_EQ(preview.files[0].filePath, "new/dir/新\nfile.cpp");
    ASSERT_EQ(preview.files[0].hunks[0].lines[0], "+new nested content");
    std::filesystem::remove_all(path);
}

TEST(empty_file_has_a_valid_first_caret_position) {
    ecs::FilePageRequest request{ecs::FilePageRequest::Action::TargetLine, {}, 1};
    file_page::Collector first(request, "auto", "");
    first.finish();
    ASSERT_TRUE(first.error.empty());
    ASSERT_TRUE(first.raw.empty());
    request.targetLine = 2;
    file_page::Collector beyond(request, "auto", "");
    beyond.finish();
    ASSERT_FALSE(beyond.error.empty());
    request.targetLine = 1;
    request.targetColumn = 2;
    file_page::Collector column(request, "auto", "");
    column.finish();
    ASSERT_FALSE(column.error.empty());
}

TEST(source_page_cursors_preserve_lexical_state_for_utf8_utf16_and_direct_jumps) {
    char directory[] = "/tmp/fh-lexical-pages.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    std::string text;
    for (int i = 1; i <= 4300; ++i) text += i == 4090 ? "/* opening\r\n" : i == 4120 ? "closing */\r\n" : "body\r\n";
    for (bool utf16 : {false, true}) {
        const std::string name = utf16 ? "wide.cpp" : "utf8.cpp";
        std::ofstream output(std::filesystem::path(path) / name, std::ios::binary);
        if (utf16) {
            output.write("\xff\xfe", 2);
            for (char ch : text) { output.put(ch); output.put('\0'); }
        } else output << text;
        output.close();
        auto first = git::read_file({path, name});
        ASSERT_TRUE(first.error.empty());
        ASSERT_EQ(first.page.next.line, 4097);
        ASSERT_EQ(first.page.next.lexical.mode, code_lexer::Mode::BlockComment);
        auto next = git::read_file({path, name, "", {ecs::FilePageRequest::Action::Next, first.page.next, 0, first.page.sourceIdentity}});
        ASSERT_TRUE(next.error.empty());
        ASSERT_EQ(next.page.begin.lexical, first.page.next.lexical);
        ASSERT_EQ(next.diff.hunks.front().syntaxAfter.front().mode, code_lexer::Mode::BlockComment);
        ASSERT_EQ(next.diff.hunks.front().syntaxAfter[24].mode, code_lexer::Mode::Code);
        auto target = git::read_file({path, name, "", {ecs::FilePageRequest::Action::TargetLine, {}, 4100}});
        ASSERT_TRUE(target.error.empty());
        ASSERT_EQ(target.page.begin.lexical.mode, code_lexer::Mode::BlockComment);
        auto previous = git::read_file({path, name, "", {ecs::FilePageRequest::Action::Previous, next.page.begin, 0, next.page.sourceIdentity}});
        ASSERT_TRUE(previous.error.empty());
        ASSERT_EQ(previous.page.begin.lexical, code_lexer::State{});
        ASSERT_EQ(previous.page.next.lexical, next.page.begin.lexical);
    }
    std::filesystem::remove_all(path);
}

TEST(a_comment_opener_split_at_the_byte_limit_keeps_its_pending_delimiter) {
    char directory[] = "/tmp/fh-lexical-fragment.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    { std::ofstream output(std::filesystem::path(path) / "fragment.cpp");
      output << std::string(file_page::byteLimit - 1, ' ') << "/* body\nend */\n"; }
    auto first = git::read_file({path, "fragment.cpp"});
    ASSERT_TRUE(first.error.empty());
    ASSERT_EQ(first.raw.size(), file_page::byteLimit);
    ASSERT_EQ(first.page.next.lexical.mode, code_lexer::Mode::BlockComment);
    ASSERT_EQ(first.page.next.lexical.skip, 1);
    auto next = git::read_file({path, "fragment.cpp", "", {ecs::FilePageRequest::Action::Next, first.page.next, 0, first.page.sourceIdentity}});
    ASSERT_TRUE(next.error.empty());
    ASSERT_EQ(next.raw, "* body\nend */\n");
    ASSERT_EQ(next.diff.hunks.front().syntaxAfter.front(), first.page.next.lexical);
    ASSERT_EQ(next.page.next.lexical, code_lexer::State{});
    std::filesystem::remove_all(path);
}

TEST(blob_page_keys_distinguish_languages_and_incoming_lexical_states) {
    git::FileRequest cpp{"repo", "file.cpp", "HEAD"};
    auto json = cpp;
    json.path = "file.json";
    ASSERT_NE(git::blob_page_key(cpp, "blob"), git::blob_page_key(json, "blob"));
    auto continuation = cpp;
    continuation.page.cursor.lexical = code_lexer::scan("/*", code_lexer::Language::Cpp);
    ASSERT_NE(git::blob_page_key(cpp, "blob"), git::blob_page_key(continuation, "blob"));
}


TEST(diff_prefix_seeds_follow_each_revision_and_renamed_language) {
    char directory[] = "/tmp/fh-diff-syntax.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    for (const auto& args : std::vector<std::vector<std::string>>{{"init", "-q"},
            {"config", "user.name", "Syntax"}, {"config", "user.email", "syntax@example.invalid"},
            {"config", "commit.gpgsign", "false"}}) ASSERT_TRUE(git::git_run(path, args).success());
    auto write = [&](const std::string& name, bool comment) {
        std::ofstream file(std::filesystem::path(path) / name);
        for (int line = 1; line <= 9000; ++line)
            file << (line == 2 ? (comment ? "/*" : "//") : line == 8990 ? (comment ? "*/" : "//") : "body") << '\n';
    };
    write("before.cpp", true);
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(path, {"commit", "-qm", "before"}).success());
    write("after.ts", false);
    const std::vector<std::pair<int, int>> starts{{4090, 4090}, {8200, 8200}};
    auto seeds = git::read_diff_syntax({path, "before.cpp", "HEAD"}, {path, "after.ts"}, starts);
    ASSERT_TRUE(seeds.error.empty());
    ASSERT_EQ(seeds.seeds.size(), 2u);
    for (const auto& [before, after] : seeds.seeds) {
        ASSERT_EQ(before.mode, code_lexer::Mode::BlockComment);
        ASSERT_EQ(after.mode, code_lexer::Mode::Code);
    }
    ecs::DiffHunk hunk;
    hunk.lines = {"-body", "+body", " body"};
    hunk_syntax::annotate(hunk, "after.ts", false, seeds.seeds[0].first, seeds.seeds[0].second, "before.cpp");
    ASSERT_EQ(hunk_syntax::at(hunk, 2, true).mode, code_lexer::Mode::BlockComment);
    ASSERT_EQ(hunk_syntax::at(hunk, 2, false).mode, code_lexer::Mode::Code);
    ecs::DiffHunk rename;
    rename.lines = {" \"\"\"", " body"};
    hunk_syntax::annotate(rename, "after.cpp", false, {}, {}, "before.py");
    ASSERT_EQ(hunk_syntax::at(rename, 1, true).mode, code_lexer::Mode::TripleDouble);
    ASSERT_EQ(hunk_syntax::at(rename, 1, false).mode, code_lexer::Mode::Code);
    auto missing = git::read_diff_syntax({path, "missing.cpp", "HEAD"}, {path, "after.ts"}, starts);
    ASSERT_FALSE(missing.error.empty());
    ASSERT_TRUE(missing.seeds.empty());
    std::stop_source stopped;
    stopped.request_stop();
    auto cancelled = git::read_diff_syntax({path, "before.cpp", "HEAD"}, {path, "after.ts"}, starts, stopped.get_token());
    ASSERT_FALSE(cancelled.error.empty());
    ASSERT_TRUE(cancelled.seeds.empty());
    std::filesystem::remove_all(path);
}

TEST(diff_syntax_publication_rejects_stale_document_generation_and_file_identity) {
    for (int stale = 0; stale < 5; ++stale) {
        ecs::RepoComponent repo;
        repo.repoPath = "/unused/syntax";
        navigation::open(repo, reading::review("wt"));
        ecs::FileDiff file;
        file.filePath = "file.cpp";
        file.syntaxResolved = true;
        file.hunks.emplace_back();
        file.hunks.back().lines = {" body"};
        std::vector<ecs::FileDiff> files{file};
        auto& runtime = repo.diffSyntax["wt"];
        runtime.identity = file.renderIdentity;
        runtime.request = navigation::stamp(repo, "syntax:" + std::to_string(file.renderIdentity));
        std::promise<ecs::DiffSyntaxResult> promise;
        runtime.future = {promise.get_future(), std::stop_source{}};
        ecs::DiffSyntaxResult result;
        result.seeds.emplace_back(code_lexer::scan("/*", code_lexer::Language::Cpp), code_lexer::State{});
        promise.set_value(std::move(result));
        if (stale == 0) repo.repoPath += "-changed";
        if (stale == 1) ++repo.dataGeneration;
        if (stale == 2) files[0].renderIdentity = ecs::next_render_identity();
        if (stale == 3) navigation::open(repo, reading::review("index"));
        ui::diff_syntax::update(repo, files, "wt");
        ASSERT_FALSE(runtime.future.valid());
        if (stale < 4) ASSERT_TRUE(files[0].hunks[0].syntaxBefore.empty());
        else ASSERT_EQ(hunk_syntax::at(files[0].hunks[0], 0, true).mode, code_lexer::Mode::BlockComment);
    }
}

TEST(diff_parser_keeps_side_states_independent_across_context_lines) {
    auto files = git::parse_diff("diff --git a/test.cpp b/test.cpp\n--- a/test.cpp\n+++ b/test.cpp\n@@ -1,3 +1,3 @@\n-/*\n+//\n body\n-*/\n+//\n");
    ASSERT_EQ(files.size(), 1u);
    ASSERT_TRUE(files[0].syntaxResolved);
    const auto& hunk = files[0].hunks[0];
    ASSERT_EQ(hunk_syntax::at(hunk, 2, true).mode, code_lexer::Mode::BlockComment);
    ASSERT_EQ(hunk_syntax::at(hunk, 2, false).mode, code_lexer::Mode::Code);
}

TEST(historical_metadata_lookup_is_literal_bounded_and_rejects_missing_objects) {
    char directory[] = "/tmp/fh-tree-metadata.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    for (const auto& args : std::vector<std::vector<std::string>>{{"init", "-q"},
            {"config", "user.name", "Metadata"}, {"config", "user.email", "metadata@example.invalid"},
            {"config", "commit.gpgsign", "false"}}) ASSERT_TRUE(git::git_run(path, args).success());
    const std::vector<std::string> names{"plain.cpp", "colon:name.cpp", "-dash.cpp", "space tab\tline\nλ.cpp", "[glob]*.cpp", "empty"};
    for (const auto& name : names) {
        std::ofstream file(std::filesystem::path(path) / name);
        if (name != "empty") file << "int metadata_unique = 42;\n";
    }
    std::filesystem::create_symlink("plain.cpp", std::filesystem::path(path) / "link");
    ASSERT_TRUE(git::git_run(path, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(path, {"commit", "-qm", "metadata"}).success());
    auto oid = git::git_run(path, {"rev-parse", "HEAD"}).stdout_str();
    oid.pop_back();
    for (const auto& name : names) {
        auto cold = git::read_file({path, name, oid});
        ASSERT_TRUE(cold.error.empty());
        ASSERT_EQ(cold.diff.newMode, "100644");
        ASSERT_EQ(cold.page.totalBytes, cold.raw.size());
        auto warm = git::read_file({path, name, oid});
        ASSERT_TRUE(warm.error.empty());
        ASSERT_EQ(warm.raw, cold.raw);
        ASSERT_TRUE(warm.trace.cacheHit);
        ASSERT_EQ(warm.trace.gitCommands, 1u);
        ASSERT_TRUE(warm.trace.validationMs >= 0.);
        ASSERT_TRUE(warm.trace.finished >= warm.trace.started);
    }
    auto link = git::read_file({path, "link", oid});
    ASSERT_TRUE(link.error.empty());
    ASSERT_EQ(link.diff.newMode, "120000");
    ASSERT_EQ(link.raw, "plain.cpp");
    ASSERT_FALSE(git::read_file({path, "absent", oid}).error.empty());
    ASSERT_TRUE(git::git_run(path, {"update-index", "--add", "--cacheinfo", "160000", oid, "module"}).success());
    ASSERT_TRUE(git::git_run(path, {"commit", "-qm", "gitlink"}).success());
    ASSERT_FALSE(git::read_file({path, "module", "HEAD"}).error.empty());
    auto cached = git::read_file({path, "plain.cpp", oid});
    const auto object = std::filesystem::path(path) / ".git/objects" / cached.page.blob.substr(0, 2) / cached.page.blob.substr(2);
    ASSERT_TRUE(std::filesystem::remove(object));
    ASSERT_FALSE(git::read_file({path, "plain.cpp", oid}).error.empty());
    std::filesystem::remove_all(path);
}

TEST(historical_source_reads_in_a_shallow_clone_without_parent_objects) {
    char directory[] = "/tmp/fh-shallow-source.XXXXXX";
    auto* root = mkdtemp(directory);
    ASSERT_TRUE(root != nullptr);
    const auto origin = std::filesystem::path(root) / "origin";
    const auto shallow = std::filesystem::path(root) / "shallow";
    std::filesystem::create_directory(origin);
    for (const auto& args : std::vector<std::vector<std::string>>{{"init", "-q"},
            {"config", "user.name", "Shallow"}, {"config", "user.email", "shallow@example.invalid"},
            {"config", "commit.gpgsign", "false"}}) ASSERT_TRUE(git::git_run(origin.string(), args).success());
    for (int revision = 0; revision < 2; ++revision) {
        std::ofstream(origin / "source.cpp") << "int shallow = " << revision << ";\n";
        ASSERT_TRUE(git::git_run(origin.string(), {"add", "."}).success());
        ASSERT_TRUE(git::git_run(origin.string(), {"commit", "-qm", "source"}).success());
    }
    ASSERT_TRUE(git::git_run(root, {"clone", "-q", "--depth=1", "file://" + origin.string(), shallow.string()}).success());
    ASSERT_EQ(git::git_run(shallow.string(), {"rev-parse", "--is-shallow-repository"}).stdout_str(), "true\n");
    auto oid = git::git_run(shallow.string(), {"rev-parse", "HEAD"}).stdout_str();
    oid.pop_back();
    for (int read = 0; read < 2; ++read) {
        auto result = git::read_file({shallow.string(), "source.cpp", oid});
        ASSERT_TRUE(result.error.empty());
        ASSERT_EQ(result.raw, "int shallow = 1;\n");
        if (read) ASSERT_EQ(result.trace.gitCommands, 1u);
    }
    ASSERT_FALSE(git::read_file({shallow.string(), "source.cpp", "HEAD^"}).error.empty());
    std::filesystem::remove_all(root);
}

int main() { RUN_ALL_TESTS(); }
