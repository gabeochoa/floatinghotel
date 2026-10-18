#include "test_framework.h"
#include "../../src/util/commit_message.h"

using commit_message::strip_template;
using commit_message::with_type;

TEST(template_drops_comment_lines_and_trailing_blanks_but_keeps_body_spacing) {
    ASSERT_EQ(strip_template("# Why?\nSubject\n\n# details\nBody line\n\n\n"), std::string("Subject\n\nBody line"));
    ASSERT_EQ(strip_template("# only instructions\n#\n"), std::string());
    ASSERT_EQ(strip_template("Ticket: \r\n"), std::string("Ticket:"));
}

TEST(type_is_inserted_or_replaces_an_existing_prefix_and_scope) {
    ASSERT_EQ(with_type("", "feat"), std::string("feat: "));
    ASSERT_EQ(with_type("add parser", "feat"), std::string("feat: add parser"));
    ASSERT_EQ(with_type("fix: add parser", "feat"), std::string("feat: add parser"));
    ASSERT_EQ(with_type("fix(io)!:  add parser\n\nbody", "docs"), std::string("docs: add parser\n\nbody"));
}

TEST(text_that_only_looks_like_a_prefix_is_left_alone) {
    ASSERT_EQ(with_type("Note: capital", "fix"), std::string("fix: Note: capital"));
    ASSERT_EQ(with_type("see(this\n): odd", "fix"), std::string("fix: see(this\n): odd"));
    ASSERT_EQ(with_type("http://x", "fix"), std::string("fix: http://x"));
}

int main() { RUN_ALL_TESTS(); }
