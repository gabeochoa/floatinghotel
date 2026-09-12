#include "repository_search.h"
#include "git_parser.h"

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

}
