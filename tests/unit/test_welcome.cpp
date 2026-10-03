#include "test_framework.h"
#include "../../src/ui/welcome.h"

using namespace ui::welcome;

TEST(plain_path_uses_last_component_as_name) {
    ASSERT_STREQ(repo_display_name("/Users/x/p/floatinghotel"), "floatinghotel");
    ASSERT_STREQ(repo_display_parent("/Users/x/p/floatinghotel", "/Users/x"),
                 "~/p");
}

TEST(dot_and_trailing_slash_forms_name_the_real_directory) {
    ASSERT_STREQ(repo_display_name("/Users/x/p/wm_afterhours/."), "wm_afterhours");
    ASSERT_STREQ(repo_display_name("/Users/x/p/wm_afterhours/./"), "wm_afterhours");
    ASSERT_STREQ(repo_display_name("/Users/x/p/wm_afterhours/"), "wm_afterhours");
    ASSERT_STREQ(repo_display_parent("/Users/x/p/wm_afterhours/.", "/Users/x"),
                 "~/p");
}

TEST(parent_outside_home_is_not_rewritten) {
    ASSERT_STREQ(repo_display_name("/tmp/fhclone"), "fhclone");
    ASSERT_STREQ(repo_display_parent("/tmp/fhclone", "/Users/x"), "/tmp");
    ASSERT_STREQ(repo_display_parent("/Users/xavier/repo", "/Users/x"),
                 "/Users/xavier");
}

TEST(root_and_empty_paths_do_not_become_dot) {
    ASSERT_STREQ(repo_display_name("/"), "/");
    ASSERT_STREQ(repo_display_name(""), "");
}

TEST(initial_is_first_alphanumeric_uppercased) {
    ASSERT_STREQ(repo_initial("floatinghotel"), "F");
    ASSERT_STREQ(repo_initial(".hidden"), "H");
    ASSERT_STREQ(repo_initial(""), "?");
}

int main() { RUN_ALL_TESTS(); }
