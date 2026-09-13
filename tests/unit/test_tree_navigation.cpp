#include "test_framework.h"
#include "../../src/util/tree_navigation.h"

using namespace file_tree;

TEST(arrows_skip_collapsed_descendants) {
    const std::set<std::string> collapsed{"src/lib/"};
    const auto rows = flatten({"src/lib/a.cpp", "src/lib/b.cpp", "src/c.cpp", "other/z.cpp"}, collapsed);
    auto next = navigate(rows, collapsed, "src/lib/", Key::Down);
    ASSERT_EQ(next->path, std::string("src/c.cpp"));
    ASSERT_TRUE(next->open);
    ASSERT_FALSE(next->keep);
    ASSERT_EQ(navigate(rows, collapsed, "src/c.cpp", Key::Up)->path, std::string("src/lib/"));
}

TEST(left_collapses_a_folder_then_moves_to_its_visible_parent) {
    auto rows = flatten({"src/lib/a.cpp", "src/lib/b.cpp"}, {});
    ASSERT_EQ(navigate(rows, {}, "src/lib/a.cpp", Key::Left)->path, std::string("src/lib/"));
    ASSERT_EQ(navigate(rows, {}, "src/lib/", Key::Left)->toggle, std::optional{std::string("src/lib/")});
    const std::set<std::string> collapsed{"src/lib/"};
    rows = flatten({"src/lib/a.cpp", "src/lib/b.cpp"}, collapsed);
    ASSERT_EQ(navigate(rows, collapsed, "src/lib/", Key::Left)->path, std::string("src/"));
}

TEST(right_expands_then_enters_a_folder) {
    const std::vector<std::string> paths{"src/λ.cpp", "src/雪.cpp"};
    const std::set<std::string> collapsed{"src/"};
    auto rows = flatten(paths, collapsed);
    ASSERT_EQ(navigate(rows, collapsed, "src/", Key::Right)->toggle, std::optional{std::string("src/")});
    rows = flatten(paths, {});
    auto entered = navigate(rows, {}, "src/", Key::Right);
    ASSERT_EQ(entered->path, std::string("src/λ.cpp"));
    ASSERT_TRUE(entered->open);
    ASSERT_FALSE(navigate(rows, {}, "src/λ.cpp", Key::Right).has_value());
}

TEST(enter_keeps_files_and_toggles_directories) {
    auto rows = flatten({"src/a.cpp"}, {});
    auto file = navigate(rows, {}, "src/a.cpp", Key::Enter);
    ASSERT_TRUE(file->open);
    ASSERT_TRUE(file->keep);
    ASSERT_EQ(navigate(rows, {}, "src/", Key::Enter)->toggle, std::optional{std::string("src/")});
}

TEST(empty_missing_and_endpoint_positions_are_bounded) {
    ASSERT_FALSE(navigate({}, {}, "missing", Key::Down).has_value());
    auto rows = flatten({"a.cpp", "b.cpp"}, {});
    ASSERT_FALSE(navigate(rows, {}, "a.cpp", Key::Up).has_value());
    ASSERT_FALSE(navigate(rows, {}, "b.cpp", Key::Down).has_value());
    ASSERT_EQ(navigate(rows, {}, "missing", Key::Down)->path, std::string("a.cpp"));
    ASSERT_EQ(navigate(rows, {}, "missing", Key::Up)->path, std::string("b.cpp"));
    ASSERT_TRUE(navigate(rows, {}, "", Key::Enter)->keep);
}

TEST(parent_lookup_uses_directory_boundaries) {
    auto rows = flatten({"src/a.cpp", "src2/b.cpp"}, {});
    ASSERT_EQ(navigate(rows, {}, "src2/b.cpp", Key::Left)->path, std::string("src2/"));
}

int main() { RUN_ALL_TESTS(); }
