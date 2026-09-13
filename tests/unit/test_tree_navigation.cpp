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

TEST(typing_matches_visible_basenames_and_skips_collapsed_children) {
    auto rows = flatten({"src/alpha.cpp", "src/beta.cpp", "hidden/alpine.cpp"}, {"hidden/"});
    TypeSelectState state;
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    auto selected = type_select(rows, "src/beta.cpp", state, "a", now);
    ASSERT_EQ(selected->path, std::string("src/alpha.cpp"));
    ASSERT_TRUE(selected->open);
    ASSERT_FALSE(selected->keep);
    selected = type_select(rows, selected->path, state, "a", now + std::chrono::milliseconds(10));
    ASSERT_EQ(selected->path, std::string("src/alpha.cpp"));
}

TEST(repeated_letters_cycle_and_extended_prefixes_stay_on_matches) {
    auto rows = flatten({"alpha.cpp", "alpine.cpp", "beta.cpp"}, {});
    TypeSelectState state;
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    ASSERT_EQ(type_select(rows, "beta.cpp", state, "A", now)->path, std::string("alpha.cpp"));
    ASSERT_EQ(type_select(rows, "alpha.cpp", state, "a", now)->path, std::string("alpine.cpp"));
    ASSERT_EQ(type_select(rows, "alpine.cpp", state, "a", now)->path, std::string("alpha.cpp"));
    ASSERT_EQ(type_select(rows, "alpha.cpp", state, "l", now)->path, std::string("alpha.cpp"));
    ASSERT_EQ(type_select(rows, "alpha.cpp", state, "p", now)->path, std::string("alpha.cpp"));
    ASSERT_EQ(type_select(rows, "alpha.cpp", state, "i", now)->path, std::string("alpine.cpp"));
    ASSERT_FALSE(type_select(rows, "alpine.cpp", state, "x", now).has_value());
}

TEST(prefix_expires_at_700_milliseconds) {
    auto rows = flatten({"ab.cpp", "beta.cpp"}, {});
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    TypeSelectState state;
    type_select(rows, "", state, "a", now);
    ASSERT_EQ(type_select(rows, "ab.cpp", state, "b", now + std::chrono::milliseconds(699))->path, std::string("ab.cpp"));
    ASSERT_EQ(state.prefix, std::string("ab"));
    ASSERT_EQ(type_select(rows, "ab.cpp", state, "b", now + std::chrono::milliseconds(1399))->path, std::string("beta.cpp"));
    ASSERT_EQ(state.prefix, std::string("b"));
    ASSERT_EQ(type_select(rows, "beta.cpp", state, "a", now)->path, std::string("ab.cpp"));
}

TEST(typing_uses_unicode_bytes_and_folder_names_without_opening_them) {
    auto rows = flatten({"src/écho.cpp", "src/雪.cpp", "assets/image.png"}, {});
    TypeSelectState state;
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    ASSERT_EQ(type_select(rows, "", state, "é", now)->path, std::string("src/écho.cpp"));
    state = {};
    ASSERT_EQ(type_select(rows, "", state, "雪", now)->path, std::string("src/雪.cpp"));
    state = {};
    auto folder = type_select(rows, "", state, "a", now);
    ASSERT_EQ(folder->path, std::string("assets/"));
    ASSERT_FALSE(folder->open);
    ASSERT_FALSE(type_select({}, "", state, "a", now).has_value());
}

TEST(explicit_navigation_expands_only_destination_ancestors_once) {
    NavigationState state;
    std::set<std::string> collapsed{"src/", "src/deep/", "other/"};
    const std::vector<std::string> paths{"src/deep/a.cpp", "other/a.cpp"};
    ASSERT_TRUE(reveal_navigation(state, "repo/review", 1, paths[0], paths, collapsed));
    ASSERT_EQ(collapsed, (std::set<std::string>{"other/"}));
    ASSERT_EQ(state.revealPath, paths[0]);
    ASSERT_TRUE(state.pendingReveal);
    ASSERT_FALSE(state.pendingFocus);
    state.pendingReveal = false;
    collapsed.insert("src/");
    ASSERT_FALSE(reveal_navigation(state, "repo/review", 1, paths[0], paths, collapsed));
    ASSERT_TRUE(collapsed.contains("src/"));
    ASSERT_FALSE(state.pendingReveal);
    ASSERT_TRUE(reveal_navigation(state, "repo/review", 2, paths[1], paths, collapsed));
    ASSERT_TRUE(collapsed.contains("src/"));
    ASSERT_EQ(state.revealPath, paths[1]);
}

TEST(missing_destinations_cancel_old_reveals_without_expanding_anything) {
    NavigationState state;
    state.path = state.revealPath = "old.cpp";
    state.pendingFocus = state.pendingReveal = true;
    std::set<std::string> collapsed{"src/"};
    ASSERT_FALSE(reveal_navigation(state, "repo", 1, "src/missing.cpp", {"src/a.cpp"}, collapsed));
    ASSERT_FALSE(state.pendingReveal);
    ASSERT_FALSE(state.pendingFocus);
    ASSERT_TRUE(collapsed.contains("src/"));
}


int main() { RUN_ALL_TESTS(); }
