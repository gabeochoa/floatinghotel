#include "test_framework.h"
#include "../../src/util/source_pages.h"
#include "../../src/git/content_reader.h"
#include <filesystem>
#include <fstream>

static ecs::FilePage page(size_t begin, size_t end, int line, int nextLine, std::string identity = "version") {
    return {{begin, line, false, 1}, {end, nextLine, false, 1}, 100, "blob", "utf8", std::move(identity)};
}

TEST(source_window_appends_and_prepends_without_duplicate_bytes) {
    ecs::SourcePageWindow window{{page(0, 2, 1, 2)}, "a\n"};
    for (int i = 1; i < 5; ++i) {
        ASSERT_TRUE(source_pages::extend(window, page(i * 2, i * 2 + 2, i + 1, i + 2), std::string(1, 'a' + i) + "\n"));
        ASSERT_TRUE(source_pages::bounded(window));
        ASSERT_TRUE(window.pages.size() <= 3);
    }
    ASSERT_EQ(window.raw, "c\nd\ne\n");
    ASSERT_EQ(source_pages::range(window).begin.line, 3);
    ASSERT_EQ(source_pages::range(window).next.line, 6);
    ASSERT_TRUE(source_pages::extend(window, page(2, 4, 2, 3), "b\n"));
    ASSERT_EQ(window.raw, "b\nc\nd\n");
    ASSERT_EQ(source_pages::range(window).begin.line, 2);
    ASSERT_TRUE(source_pages::bounded(window));
}

TEST(source_window_rejects_gaps_overlap_and_changed_versions_without_mutation) {
    ecs::SourcePageWindow window{{page(0, 2, 1, 2)}, "a\n"};
    ASSERT_FALSE(source_pages::extend(window, page(4, 6, 3, 4), "c\n"));
    ASSERT_FALSE(source_pages::extend(window, page(0, 2, 1, 2), "a\n"));
    ASSERT_FALSE(source_pages::extend(window, page(2, 4, 2, 3, "new version"), "b\n"));
    ASSERT_FALSE(source_pages::extend(window, page(2, 4, 3, 4), "b\n"));
    ASSERT_FALSE(source_pages::extend(window, page(2, 4, 2, 3), "longer"));
    ASSERT_EQ(window.raw, "a\n");
    ASSERT_EQ(window.pages.size(), 1u);
}

TEST(source_window_joins_real_unicode_and_crlf_fragments_in_both_directions) {
    const auto directory = std::filesystem::temp_directory_path() / "floatinghotel_source_window_unit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "long.txt";
    std::string text = "first\r\n";
    for (int i = 0; i < 400000; ++i) text += "é";
    text += "\r\nlast";
    { std::ofstream file(path, std::ios::binary); file << text; }
    git::FileRequest request{directory.string(), "long.txt", ""};
    auto first = git::read_file(request);
    ASSERT_TRUE(first.error.empty());
    ecs::SourcePageWindow window{{first.page}, first.raw};
    while (window.pages.back().next.offset < text.size()) {
        auto range = source_pages::range(window);
        request.page = {ecs::FilePageRequest::Action::Next, range.next, 0, range.sourceIdentity};
        request.detectedEncoding = range.encoding;
        auto next = git::read_file(request);
        ASSERT_TRUE(next.error.empty());
        ASSERT_TRUE(source_pages::extend(window, next.page, next.raw));
        range = source_pages::range(window);
        ASSERT_EQ(window.raw, text.substr(range.begin.offset, range.next.offset - range.begin.offset));
        ASSERT_TRUE(source_pages::bounded(window));
        auto decoded = file_page::decode(window.raw, range.encoding, range.begin.offset);
        ASSERT_EQ(decoded.text, window.raw);
    }
    ASSERT_TRUE(window.pages.front().begin.offset > 0);
    while (window.pages.front().begin.offset > 0) {
        auto range = source_pages::range(window);
        request.page = {ecs::FilePageRequest::Action::Previous, range.begin, 0, range.sourceIdentity};
        auto previous = git::read_file(request);
        ASSERT_TRUE(previous.error.empty());
        ASSERT_TRUE(source_pages::extend(window, previous.page, previous.raw));
        range = source_pages::range(window);
        ASSERT_EQ(window.raw, text.substr(range.begin.offset, range.next.offset - range.begin.offset));
        ASSERT_TRUE(source_pages::bounded(window));
    }
    std::filesystem::remove_all(directory);
}

TEST(source_window_honors_line_limits_and_empty_files) {
    ecs::SourcePageWindow empty{{page(0, 0, 1, 1)}, ""};
    ASSERT_TRUE(source_pages::bounded(empty));
    ASSERT_FALSE(source_pages::extend(empty, page(0, 1, 1, 5000), "x"));
    ecs::SourcePageWindow tooLarge{{page(0, file_page::byteLimit + 1, 1, 2)}, std::string(file_page::byteLimit + 1, 'x')};
    ASSERT_FALSE(source_pages::bounded(tooLarge));
}

TEST(source_window_does_not_join_a_changed_working_file) {
    const auto directory = std::filesystem::temp_directory_path() / "floatinghotel_source_window_changed_unit";
    std::filesystem::create_directories(directory);
    const auto path = directory / "changed.txt";
    { std::ofstream file(path, std::ios::binary); file << std::string(300000, 'a'); }
    git::FileRequest request{directory.string(), "changed.txt", ""};
    auto first = git::read_file(request);
    ASSERT_TRUE(first.error.empty());
    ecs::SourcePageWindow window{{first.page}, first.raw};
    { std::ofstream file(path, std::ios::binary); file << std::string(400000, 'b'); }
    request.page = {ecs::FilePageRequest::Action::Next, first.page.next, 0, first.page.sourceIdentity};
    auto changed = git::read_file(request);
    ASSERT_FALSE(changed.error.empty());
    ASSERT_EQ(window.raw, first.raw);
    ASSERT_TRUE(source_pages::bounded(window));
    std::filesystem::remove_all(directory);
}

int main() { RUN_ALL_TESTS(); }
