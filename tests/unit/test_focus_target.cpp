#include "test_framework.h"
#include "../../src/util/focus_target.h"

using namespace reading::focus;

TEST(focus_returns_to_semantic_caller_after_popup_closes) {
    State state;
    Target caller{"repo", {1}, Region::Code, "a.cpp", "options"};
    state.origin = caller;
    state.sync("repo", 5, {Popup::ContextMenu});
    state.origin = Target{"repo", {1}, Region::Menu, {}, "menu_item"};
    state.sync("repo", 5, {});
    ASSERT_EQ(state.pending, std::optional{caller});
}

TEST(focus_nested_popup_returns_to_immediate_then_original_caller) {
    State state;
    Target code{"repo", {1}, Region::Code, {}, "diff_scroll"};
    Target search{"repo", {1}, Region::Search, "a.cpp:50", "repo_search_preview"};
    state.origin = code;
    state.sync("repo", 5, {Popup::Search});
    state.origin = search;
    state.sync("repo", 5, {Popup::Search, Popup::SearchPreview});
    state.sync("repo", 5, {Popup::Search});
    ASSERT_EQ(state.pending, std::optional{search});
    state.sync("repo", 5, {});
    ASSERT_EQ(state.pending, std::optional{code});
}

TEST(focus_does_not_restore_across_navigation_or_repository_changes) {
    State state;
    state.origin = Target{"repo", {1}, Region::Code, {}, "diff_scroll"};
    state.sync("repo", 5, {Popup::Picker});
    state.sync("repo", 6, {});
    ASSERT_FALSE(state.pending.has_value());
    state.sync("repo", 6, {Popup::Picker});
    state.sync("other", 6, {});
    ASSERT_FALSE(state.pending.has_value());
    ASSERT_TRUE(state.returns.empty());
}

TEST(focus_rejects_closed_documents_and_foreign_repository_targets) {
    State state;
    state.repository = "repo";
    reading::ReadingWorkspace workspace;
    ASSERT_TRUE(state.valid({"repo", {1}, Region::Code}, workspace));
    ASSERT_TRUE(state.valid({"repo", {}, Region::History}, workspace));
    ASSERT_FALSE(state.valid({"repo", {99}, Region::DocumentTabs}, workspace));
    ASSERT_FALSE(state.valid({"other", {1}, Region::Code}, workspace));
}

TEST(focus_pending_return_is_cancelled_if_navigation_changes_before_layout_is_ready) {
    State state;
    state.origin = Target{"repo", {}, Region::History, "commit", "commit_row"};
    state.sync("repo", 5, {Popup::Picker});
    state.sync("repo", 5, {});
    ASSERT_TRUE(state.pending.has_value());
    state.sync("repo", 6, {});
    ASSERT_FALSE(state.pending.has_value());
}

TEST(shortcuts_belong_to_the_focused_region) {
    ASSERT_TRUE((ShortcutOwner{Region::Code, false}.reader()));
    ASSERT_TRUE((ShortcutOwner{Region::DocumentTabs, false}.reader()));
    for (auto region : {Region::Tree, Region::History, Region::Picker, Region::Find, Region::Search,
                        Region::SearchPreview, Region::Feedback, Region::Menu})
        ASSERT_FALSE((ShortcutOwner{region, false}.reader()));
    ASSERT_FALSE((ShortcutOwner{}.reader()));
}

TEST(text_inputs_own_editing_keys_inside_every_region) {
    for (auto region : {Region::Code, Region::DocumentTabs, Region::Tree, Region::History,
                        Region::Picker, Region::Find, Region::Search, Region::Feedback}) {
        ASSERT_FALSE((ShortcutOwner{region, true}.reader()));
        ASSERT_TRUE((ShortcutOwner{region, true}.input(region)));
    }
    ASSERT_FALSE((ShortcutOwner{Region::Search, false}.input(Region::Search)));
    ASSERT_FALSE((ShortcutOwner{Region::Find, true}.input(Region::Search)));
}

int main() {
    RUN_ALL_TESTS();
}
