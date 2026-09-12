#include "repository_search.h"
#include "git_parser.h"
#include "../util/path_glob.h"
#include "../util/file_content.h"
#include <charconv>
#include <filesystem>

namespace git {

std::vector<std::string> repository_search_args(const ecs::SearchQuery& query) {
    std::vector<std::string> args{"grep", "--no-color", "-n", "-I", "-z", "--full-name"};
    args.push_back(query.matching.regularExpression ? "-E" : "-F");
    if (!query.matching.caseSensitive) args.push_back("-i");
    if (query.matching.wholeWord) args.push_back("-w");
    args.insert(args.end(), {"-e", query.text});
    if (query.revision.empty()) args.insert(args.end(), {"--untracked", "--exclude-standard"});
    else if (query.revision == "INDEX") args.push_back("--cached");
    else args.push_back(query.revision);
    args.push_back("--");
    for (const auto& path : query.paths) args.push_back(":(literal)" + path);
    if (!query.changedOnly) {
        if (!query.includeGlob.empty()) args.push_back(":(glob)" + query.includeGlob);
        if (!query.excludeGlob.empty()) args.push_back(":(glob,exclude)" + query.excludeGlob);
    }
    return args;
}

std::vector<ecs::SearchMatch> parse_search_matches(const std::string& output, const std::string& revision) {
    auto matches = parse_grep_matches(output);
    for (auto& match : matches) {
        auto prefix = revision + ":";
        if (!revision.empty() && revision != "INDEX" && match.file.starts_with(prefix)) match.file.erase(0, prefix.size());
        match.revision = revision;
    }
    return matches;
}

async_work::Task<ecs::SearchResult> search_repository_async(ecs::SearchQuery query) {
    ecs::SearchResult rejected{query.revision, {}, "Background queue is full; search again to retry"};
    return async_work::launch([query = std::move(query)](std::stop_token stop) mutable {
        ecs::SearchResult out;
        if (query.changedOnly) {
            auto excluded = [&](const std::string& path) {
                return (!query.includeGlob.empty() && !path_glob_matches(query.includeGlob, path)) ||
                    (!query.excludeGlob.empty() && path_glob_matches(query.excludeGlob, path));
            };
            std::erase_if(query.paths, excluded);
            std::erase_if(query.removedPaths, excluded);
        }
        out.revision = query.revision;
        auto append = [&](ecs::SearchQuery part, bool primary) {
            if (!part.revision.empty() && part.revision != "INDEX") {
                auto revision = git_run(part.repoPath, {"rev-parse", "--verify", "--end-of-options", part.revision + "^{commit}"}, stop);
                if (!revision.success()) { out.error = revision.stderr_str(); return; }
                part.revision = revision.stdout_str();
                while (!part.revision.empty() && (part.revision.back() == '\n' || part.revision.back() == '\r')) part.revision.pop_back();
            }
            if (primary) out.revision = part.revision;
            auto result = git_run(part.repoPath, repository_search_args(part), stop);
            if (!result.success() && result.exit_code() != 1) out.error = "Search failed: " + result.stderr_str();
            else for (auto& match : parse_search_matches(result.stdout_str(), part.revision))
                if (out.matches.size() < 5000) out.matches.push_back(std::move(match));
        };
        if (!query.changedOnly || !query.paths.empty()) append(query, true);
        if (query.changedOnly && !query.removedPaths.empty()) {
            query.revision = query.beforeRevision;
            query.paths = std::move(query.removedPaths);
            append(std::move(query), false);
        }
        return out;
    }, async_work::Priority::Foreground, std::move(rejected));
}

ecs::SearchPreview search_preview_lines(const std::string& bytes, ecs::SearchMatch match) {
    ecs::SearchPreview out{std::move(match)};
    int line = 1;
    size_t start = 0;
    bool found = false;
    while (start < bytes.size()) {
        auto end = bytes.find('\n', start);
        if (end == std::string::npos) end = bytes.size();
        auto text = bytes.substr(start, end - start);
        if (line == out.match.line) {
            found = true;
            out.changedSinceSearch = text != out.match.text;
        }
        if (line >= out.match.line - 2 && line <= out.match.line + 2)
            out.lines.emplace_back(line, std::move(text));
        if (line >= out.match.line + 2) break;
        start = end + 1;
        ++line;
    }
    if (!found) out.error = "The result line no longer exists; search again";
    return out;
}

async_work::Task<ecs::SearchPreview> search_preview_async(std::string repoPath, ecs::SearchMatch match) {
    ecs::SearchPreview rejected{match, {}, "Background queue is full; reopen the preview to retry"};
    return async_work::launch([repoPath = std::move(repoPath), match = std::move(match)](std::stop_token stop) {
        constexpr size_t limit = 1024 * 1024;
        ecs::SearchPreview out{match};
        std::string bytes;
        if (match.revision.empty()) {
            auto path = std::filesystem::path(repoPath) / match.file;
            auto content = file_content::read_working_file(path, stop, limit);
            if (!content.error.empty()) {
                out.error = content.error == "File exceeds the read size limit"
                    ? "Preview limited to files up to 1 MiB; open the file to read it"
                    : std::move(content.error);
                return out;
            }
            bytes = std::move(content.bytes);
        } else {
            auto source = (match.revision == "INDEX" ? "" : match.revision) + ":" + match.file;
            auto resolved = git_run(repoPath, {"rev-parse", "--verify", "--end-of-options", source}, stop);
            if (!resolved.success()) { out.error = "Preview source is no longer available"; return out; }
            auto blob = resolved.stdout_str();
            while (!blob.empty() && (blob.back() == '\n' || blob.back() == '\r')) blob.pop_back();
            auto size = git_run(repoPath, {"cat-file", "-s", blob}, stop);
            auto sizeText = size.stdout_str();
            size_t count = 0;
            auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), count);
            if (!size.success() || parsed.ec != std::errc{}) { out.error = "Unable to measure preview source"; return out; }
            if (count > limit) { out.error = "Preview limited to files up to 1 MiB; open the file to read it"; return out; }
            auto content = git_run(repoPath, {"cat-file", "blob", blob}, stop);
            if (!content.success()) { out.error = "Unable to read preview source"; return out; }
            bytes = content.stdout_str();
        }
        if (bytes.size() > limit) { out.error = "Preview limited to files up to 1 MiB; open the file to read it"; return out; }
        return search_preview_lines(bytes, match);
    }, async_work::Priority::Foreground, std::move(rejected));
}

}
