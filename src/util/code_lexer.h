#pragma once

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace code_lexer {

enum class Language { Plain, C, Cpp, Python, JavaScript, Json, Hash, Sql, Slash };
enum class Mode { Code, LineComment, BlockComment, Single, Double, Template, TripleSingle, TripleDouble, Raw };
enum class Region { Code, Comment, String };

inline constexpr size_t lookaheadSize = 24;

// Extension or exact file name -> lexer. Each lexer is a comment/string
// dialect, so a new language that shares one only needs a row here.
struct LanguageRule {
    std::string_view match;
    Language language;
};
inline constexpr LanguageRule languageRules[] = {
    {".c", Language::C}, {".m", Language::C},
    {".h", Language::Cpp}, {".cc", Language::Cpp}, {".cpp", Language::Cpp}, {".cxx", Language::Cpp},
    {".hpp", Language::Cpp}, {".hh", Language::Cpp}, {".hxx", Language::Cpp}, {".ipp", Language::Cpp},
    {".inl", Language::Cpp}, {".tpp", Language::Cpp}, {".mm", Language::Cpp}, {".cu", Language::Cpp},
    {".cuh", Language::Cpp}, {".metal", Language::Cpp},
    {".py", Language::Python}, {".pyi", Language::Python}, {".pyw", Language::Python},
    {".js", Language::JavaScript}, {".jsx", Language::JavaScript}, {".mjs", Language::JavaScript},
    {".cjs", Language::JavaScript}, {".ts", Language::JavaScript}, {".tsx", Language::JavaScript},
    {".mts", Language::JavaScript}, {".cts", Language::JavaScript},
    {".json", Language::Json},
    {".sh", Language::Hash}, {".bash", Language::Hash}, {".zsh", Language::Hash}, {".fish", Language::Hash},
    {".rb", Language::Hash}, {".rake", Language::Hash}, {".gemspec", Language::Hash}, {".pl", Language::Hash},
    {".yaml", Language::Hash}, {".yml", Language::Hash}, {".toml", Language::Hash}, {".cmake", Language::Hash},
    {".mk", Language::Hash}, {".tf", Language::Hash}, {".r", Language::Hash},
    {"Makefile", Language::Hash}, {"GNUmakefile", Language::Hash}, {"Dockerfile", Language::Hash},
    {"CMakeLists.txt", Language::Hash}, {"Gemfile", Language::Hash}, {"Rakefile", Language::Hash},
    {".gitignore", Language::Hash}, {".gitattributes", Language::Hash},
    {".sql", Language::Sql}, {".lua", Language::Sql},
    {".rs", Language::Slash}, {".go", Language::Slash}, {".java", Language::Slash}, {".kt", Language::Slash},
    {".kts", Language::Slash}, {".swift", Language::Slash}, {".cs", Language::Slash}, {".scala", Language::Slash},
    {".dart", Language::Slash}, {".groovy", Language::Slash}, {".gradle", Language::Slash},
    {".proto", Language::Slash}, {".zig", Language::Slash}, {".php", Language::Slash},
};

inline Language language(std::string_view path) {
    const auto slash = path.find_last_of('/');
    const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = name.find_last_of('.');
    std::string ext(dot == std::string_view::npos ? std::string_view{} : name.substr(dot));
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& rule : languageRules)
        if (rule.match == name || rule.match == ext) return rule.language;
    return Language::Plain;
}

struct State {
    Mode mode = Mode::Code;
    unsigned char skip = 0;
    bool closing = false;
    bool escaped = false;
    unsigned char delimiterSize = 0;
    std::array<char, 16> delimiter{};
    bool operator==(const State&) const = default;

    std::string key() const {
        std::string value{static_cast<char>(mode), static_cast<char>(skip), static_cast<char>(closing),
            static_cast<char>(escaped), static_cast<char>(delimiterSize)};
        value.append(delimiter.data(), delimiterSize);
        return value;
    }
};

inline Region region(Mode mode) {
    return mode == Mode::Code ? Region::Code : mode == Mode::LineComment || mode == Mode::BlockComment ? Region::Comment : Region::String;
}

inline Region advance_with_lookahead(State& state, Language lang, std::string_view next) {
    if (next.empty() || lang == Language::Plain) return Region::Code;
    const char ch = next.front();
    const auto current = region(state.mode);
    if (state.skip) {
        --state.skip;
        if (!state.skip && state.closing) state = {};
        return current;
    }
    auto close = [&](size_t length) {
        state.skip = static_cast<unsigned char>(length - 1);
        state.closing = true;
        if (!state.skip) state = {};
        return current;
    };
    if (state.mode == Mode::LineComment) {
        const bool splice = (lang == Language::C || lang == Language::Cpp) && state.escaped;
        if (ch == '\n') {
            if (!splice) state = {};
            else state.escaped = false;
        } else if (ch != '\r') state.escaped = ch == '\\';
        return Region::Comment;
    }
    if (state.mode == Mode::BlockComment) return next.starts_with("*/") ? close(2) : Region::Comment;
    if (state.mode == Mode::Raw) {
        if (ch == ')' && next.size() >= static_cast<size_t>(state.delimiterSize) + 2 &&
            next.substr(1, state.delimiterSize) == std::string_view(state.delimiter.data(), state.delimiterSize) &&
            next[state.delimiterSize + 1] == '"') return close(state.delimiterSize + 2);
        return Region::String;
    }
    if (state.mode != Mode::Code) {
        if (ch == '\r' && next.size() > 1 && next[1] == '\n') return Region::String;
        if (state.escaped) { state.escaped = false; return Region::String; }
        if (ch == '\\') { state.escaped = true; return Region::String; }
        if (state.mode == Mode::TripleSingle && next.starts_with("\'\'\'")) return close(3);
        if (state.mode == Mode::TripleDouble && next.starts_with("\"\"\"")) return close(3);
        if ((state.mode == Mode::Single && ch == '\'') || (state.mode == Mode::Double && ch == '"') ||
            (state.mode == Mode::Template && ch == '`')) return close(1);
        if (ch == '\n' && (state.mode == Mode::Single || state.mode == Mode::Double)) state = {};
        return Region::String;
    }
    const bool slash = lang == Language::C || lang == Language::Cpp || lang == Language::JavaScript || lang == Language::Slash;
    if ((slash && next.starts_with("//")) || ((lang == Language::Python || lang == Language::Hash) && ch == '#') ||
        (lang == Language::Sql && next.starts_with("--"))) {
        state.mode = Mode::LineComment;
        state.skip = ch == '#' ? 0 : 1;
        return Region::Comment;
    }
    if (slash && next.starts_with("/*")) {
        state.mode = Mode::BlockComment;
        state.skip = 1;
        return Region::Comment;
    }
    if (lang == Language::Cpp && next.starts_with("R\"")) {
        const auto end = next.find('(', 2);
        if (end != std::string_view::npos && end - 2 <= state.delimiter.size()) {
            const auto delimiter = next.substr(2, end - 2);
            bool valid = true;
            for (unsigned char value : delimiter)
                valid &= value > 32 && value < 127 && value != '(' && value != ')' && value != '\\';
            if (valid) {
                state.mode = Mode::Raw;
                state.skip = static_cast<unsigned char>(end);
                state.delimiterSize = static_cast<unsigned char>(delimiter.size());
                for (size_t i = 0; i < delimiter.size(); ++i) state.delimiter[i] = delimiter[i];
                return Region::String;
            }
        }
    }
    if (lang == Language::Python && (next.starts_with("\'\'\'") || next.starts_with("\"\"\""))) {
        state.mode = ch == '\'' ? Mode::TripleSingle : Mode::TripleDouble;
        state.skip = 2;
        return Region::String;
    }
    if (ch == '\'' || ch == '"' || (lang == Language::JavaScript && ch == '`')) {
        state.mode = ch == '\'' ? Mode::Single : ch == '"' ? Mode::Double : Mode::Template;
        return Region::String;
    }
    return Region::Code;
}

inline State scan(std::string_view text, Language lang, State state = {}) {
    for (size_t i = 0; i < text.size(); ++i) advance_with_lookahead(state, lang, text.substr(i, lookaheadSize));
    return state;
}

inline std::string_view line_lookahead(std::string_view text, size_t offset, bool newline) {
    return newline && offset + 1 == text.size() && text[offset] == '\r' ? std::string_view("\r\n") : text.substr(offset, lookaheadSize);
}

inline State scan_line(std::string_view text, bool newline, Language lang, State state = {}) {
    for (size_t i = 0; i < text.size(); ++i) {
        const auto next = line_lookahead(text, i, newline);
        advance_with_lookahead(state, lang, next);
    }
    if (newline) advance_with_lookahead(state, lang, "\n");
    return state;
}

}
