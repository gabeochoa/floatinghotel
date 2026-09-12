#include "repository_search.h"
#include "git_parser.h"

namespace git {

std::vector<std::string> repository_search_args(const ecs::SearchQuery& query) {
    std::vector<std::string> args{"grep", "--no-color", "-n", "-I", "-z", "--full-name", "-F", "-e", query.text};
    if (query.revision.empty()) args.insert(args.end(), {"--untracked", "--exclude-standard"});
    else if (query.revision == "INDEX") args.push_back("--cached");
    else args.push_back(query.revision);
    args.push_back("--");
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
        if (!query.revision.empty() && query.revision != "INDEX") {
            auto revision = git_run(query.repoPath, {"rev-parse", "--verify", "--end-of-options", query.revision + "^{commit}"}, stop);
            if (!revision.success()) { out.error = revision.stderr_str(); return out; }
            query.revision = revision.stdout_str();
            while (!query.revision.empty() && (query.revision.back() == '\n' || query.revision.back() == '\r')) query.revision.pop_back();
        }
        out.revision = query.revision;
        auto result = git_run(query.repoPath, repository_search_args(query), stop);
        if (!result.success() && result.exit_code() != 1) out.error = result.stderr_str();
        else out.matches = parse_search_matches(result.stdout_str(), query.revision);
        return out;
    }, async_work::Priority::Foreground, std::move(rejected));
}

}
