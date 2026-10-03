#include "test_framework.h"
#include "../../src/util/outline.h"

using symbol_outline::Symbol;

static std::vector<std::string> names(const std::vector<Symbol>& symbols) {
    std::vector<std::string> out;
    for (const auto& symbol : symbols) out.push_back(symbol.name + "@" + std::to_string(symbol.line));
    return out;
}

TEST(cpp_types_functions_and_multiline_declarations) {
    const auto symbols = symbol_outline::scan("a.cpp",
        "#include <x>\n"
        "#define MACRO(a) { a }\n"
        "namespace app {\n"
        "struct Point { int x; };\n"
        "class Widget : public Base {\n"
        "public:\n"
        "    void draw() const override {\n"
        "        if (x) { run(); }\n"
        "        std::sort(a, b, [](int l, int r) { return l < r; });\n"
        "    }\n"
        "};\n"
        "static int\n"
        "compute(int a,\n"
        "        int b)\n"
        "{\n"
        "    return a + b;\n"
        "}\n"
        "Widget::Widget(int v) : value_(v), other_{v} {}\n"
        "template <typename T> T twice(T v) { return v * 2; }\n"
        "struct Point* make_point() { return nullptr; }\n"
        "enum class Color : int { Red };\n"
        "}\n");
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"app@3", "Point@4", "Widget@5", "draw@7", "compute@13",
        "Widget::Widget@18", "twice@19", "make_point@20", "Color@21"}));
    ASSERT_EQ(symbols[0].kind, std::string("namespace"));
    ASSERT_EQ(symbols[3].kind, std::string("function"));
}

TEST(comments_and_strings_are_not_symbols) {
    const auto symbols = symbol_outline::scan("a.c",
        "// void commented() {\n"
        "/* struct Hidden {\n"
        "   int fake() { */\n"
        "const char* s = \"int quoted() {\";\n"
        "int real(void) {\n"
        "    return 0;\n"
        "}\n");
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"real@5"}));
}

TEST(duplicates_and_symbols_far_down_the_file_are_kept) {
    std::string text = "void overload(int) {}\nvoid overload(double) {}\n";
    for (int i = 0; i < 20000; ++i) text += "int filler_" + std::to_string(i) + " = 0;\n";
    text += "void last() {}\n";
    const auto symbols = symbol_outline::scan("a.cpp", text);
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"overload@1", "overload@2", "last@20003"}));
}

TEST(python_definitions) {
    const auto symbols = symbol_outline::scan("a.py",
        "class Model:\n"
        "    def fit(self):\n"
        "        s = \"def fake():\"\n"
        "    async def load(self):\n"
        "        pass\n"
        "# def commented():\n");
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"Model@1", "fit@2", "load@4"}));
}

TEST(javascript_and_typescript_forms) {
    const auto symbols = symbol_outline::scan("a.ts",
        "export interface Props {\n"
        "  name: string;\n"
        "}\n"
        "export function render(props: Props): string {\n"
        "  items.forEach(item => {\n"
        "    if (item) {}\n"
        "  });\n"
        "  return '';\n"
        "}\n"
        "const handler = async (event) => {\n"
        "};\n"
        "const config = {\n"
        "};\n"
        "class Store {\n"
        "  load() {\n"
        "  }\n"
        "}\n"
        "describe('suite', () => {\n"
        "});\n");
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"Props@1", "render@4", "handler@10", "Store@14", "load@15"}));
}

TEST(go_rust_and_kotlin_definitions) {
    ASSERT_EQ(names(symbol_outline::scan("a.go",
        "type Server struct {\n"
        "}\n"
        "func (s *Server) Serve(addr string) error {\n"
        "    for i := 0; i < 3; i++ {\n"
        "    }\n"
        "}\n"
        "func main() {\n"
        "}\n")), (std::vector<std::string>{"Serve@3", "main@7"}));
    ASSERT_EQ(names(symbol_outline::scan("a.rs",
        "pub struct Parser {\n"
        "}\n"
        "impl Parser {\n"
        "    pub fn parse(&self, input: &str) -> Result<(), Error> {\n"
        "        match input {\n"
        "        }\n"
        "    }\n"
        "}\n")), (std::vector<std::string>{"Parser@1", "Parser@3", "parse@4"}));
    ASSERT_EQ(names(symbol_outline::scan("a.kt",
        "class Repo(val path: String) {\n"
        "    fun load(): List<String> {\n"
        "    }\n"
        "}\n")), (std::vector<std::string>{"Repo@1", "load@2"}));
}

TEST(markdown_headings_outside_fences) {
    const auto symbols = symbol_outline::scan("README.md",
        "# Title\n"
        "text\n"
        "```sh\n"
        "# not a heading\n"
        "```\n"
        "## Usage ##\n"
        "#hashtag\n");
    ASSERT_EQ(names(symbols), (std::vector<std::string>{"# Title@1", "## Usage@6"}));
}

TEST(unsupported_languages_have_no_outline) {
    ASSERT_TRUE(symbol_outline::scan("notes.txt", "void f() {}\n").empty());
    ASSERT_FALSE(symbol_outline::supported("data.json"));
}

int main() {
    RUN_ALL_TESTS();
}
