#include "test_framework.h"
#include "src/util/source_folding.h"

static std::vector<source_folding::Range> folds(const std::string& text, const std::string& path = "file.cpp", bool atEnd = true) {
    std::vector<reading::CodeLine> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back({static_cast<int>(lines.size()) + 1, 1, std::string_view(text).substr(start, end - start)});
        start = end + 1;
    }
    return source_folding::discover(path, lines, {}, atEnd);
}

TEST(brace_ranges_ignore_comments_strings_raw_literals_and_one_line_blocks) {
    auto ranges = folds("void f() {\n /* } */\n auto x = R\"tag( } { )tag\";\n if (x) {\n  use();\n }\n}\nvoid g() {}\n");
    ASSERT_EQ(ranges, (std::vector<source_folding::Range>{{1, 7}, {4, 6}}));
    for (const auto& path : {"file.c", "file.m", "file.mm", "file.js", "file.ts", "file.json"})
        ASSERT_EQ(folds("{\n \"}\"\n}\n", path), (std::vector<source_folding::Range>{{1, 3}}));
}

TEST(incomplete_and_ambiguous_ranges_remain_unfolded) {
    ASSERT_TRUE(folds("void f() {\n body\n").empty());
    ASSERT_TRUE(folds("function f() {\n const x = /[{}]/;\n}\n", "file.js").empty());
    ASSERT_TRUE(folds("function f() {\n const x = `a${nested}`;\n}\n", "file.ts").empty());
    ASSERT_TRUE(folds("{\nbody\n}\n", "file.txt").empty());
    ASSERT_EQ(folds("function f() {\n const x = `}\\n{`;\n}\n", "file.ts"), (std::vector<source_folding::Range>{{1, 3}}));
}

TEST(python_suites_require_indentation_and_a_proven_end) {
    const std::string text = "def f():\n    if ready:\n        body()\n    other()\nnext()\n";
    ASSERT_EQ(folds(text, "file.py"), (std::vector<source_folding::Range>{{1, 5}, {2, 4}}));
    ASSERT_EQ(folds("def f():\n    body()\n", "file.py"), (std::vector<source_folding::Range>{{1, 3}}));
    ASSERT_TRUE(folds("def f():\n    body()\n", "file.py", false).empty());
    ASSERT_TRUE(folds("def f():\nbody()\n", "file.py").empty());
    ASSERT_TRUE(folds("def f():\n\tbody()\n", "file.py").empty());
    ASSERT_EQ(folds("def f():\n    text = \"\"\"\nif fake:\n    text\n\"\"\"\n    body()\nnext()\n", "file.py"),
        (std::vector<source_folding::Range>{{1, 7}}));
}

TEST(fragment_prefixes_and_nested_destination_reveal_are_safe) {
    std::vector<reading::CodeLine> lines{{4096, 4, "{ "}, {4097, 1, "body"}, {4098, 1, "}"}};
    ASSERT_TRUE(source_folding::discover("file.cpp", lines).empty());
    lines[0].column = 1;
    ASSERT_EQ(source_folding::discover("file.cpp", lines), (std::vector<source_folding::Range>{{4096, 4098}}));
    source_folding::State state;
    state.sync("first");
    ASSERT_TRUE(state.toggle({1, 10}));
    ASSERT_TRUE(state.toggle({3, 7}));
    state.reveal(5);
    ASSERT_TRUE(state.folded.empty());
    state.toggle({1, 10});
    state.reveal(10);
    ASSERT_EQ(state.folded.size(), 1u);
    state.sync("first");
    ASSERT_EQ(state.folded.size(), 1u);
    state.sync("changed");
    ASSERT_TRUE(state.folded.empty());
}


TEST(fold_metadata_is_bounded_and_resets_only_when_the_source_changes) {
    source_folding::State state;
    state.sync("blob");
    for (int i = 0; i < 512; ++i) ASSERT_TRUE(state.toggle({i * 3 + 1, i * 3 + 3}));
    ASSERT_FALSE(state.can_toggle({2000, 2010}));
    ASSERT_TRUE(state.can_toggle({1, 3}));
    ASSERT_FALSE(state.toggle({2000, 2010}));
    ASSERT_TRUE(state.toggle({1, 3}));
    ASSERT_TRUE(state.toggle({2000, 2010}));
    state.sync("blob");
    ASSERT_EQ(state.folded.size(), 512u);
    state.sync("replacement");
    ASSERT_TRUE(state.folded.empty());
}


TEST(later_ambiguity_does_not_discard_already_proven_ranges) {
    ASSERT_EQ(folds("function f() {\n call();\n}\nlet x = /[{}]/;\n", "file.js"),
        (std::vector<source_folding::Range>{{1, 3}}));
    ASSERT_EQ(folds("void f() {\n/*\n}\n*/\n}\n"), (std::vector<source_folding::Range>{{1, 5}}));
    ASSERT_EQ(folds("void f() {\nauto text = R\"x(\n}\n)x\";\n}\n"), (std::vector<source_folding::Range>{{1, 5}}));
    ASSERT_EQ(folds("function f() {\nconst text = `}\n{`;\n}\n", "file.js"),
        (std::vector<source_folding::Range>{{1, 4}}));
}


TEST(escaped_crlf_strings_cannot_create_brace_ranges) {
    const std::string text = "const char* text = \"\\\r\n{ \\\r\nignored \\\r\n}\";\r\n";
    ASSERT_TRUE(folds(text).empty());
}

int main() { RUN_ALL_TESTS(); }
