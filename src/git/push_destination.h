#pragma once

#include "reading_catalog.h"
#include "log_redaction.h"
#include "../util/storage_key.h"

namespace git {

struct PushDestination {
    std::string branch;
    std::string remote;
    std::string destination;
    std::string fetchUrl;
    std::string pushUrl;
    std::vector<std::string> remotes;
    std::string error;
    std::string pushIdentity;
};

inline PushDestination read_push_destination(const std::string& repository, std::string chosenRemote, std::stop_token stop) {
    PushDestination result;
    auto branch = git_run(repository, {"symbolic-ref", "--quiet", "--short", "HEAD"}, stop);
    if (!branch.success()) { result.error = "Select a local branch before pushing"; return result; }
    result.branch = catalog::trim_line(branch.stdout_str());
    auto remotes = git_run(repository, {"remote"}, stop);
    if (!remotes.success()) { result.error = remotes.stderr_str(); return result; }
    result.remotes = catalog::fields(remotes.stdout_str(), '\n');
    auto config = [&](const std::string& key) {
        auto value = git_run(repository, {"config", "--get", key}, stop);
        return value.success() ? catalog::trim_line(value.stdout_str()) : std::string{};
    };
    const auto trackingRemote = config("branch." + result.branch + ".remote");
    result.remote = std::move(chosenRemote);
    if (result.remote.empty()) result.remote = config("branch." + result.branch + ".pushRemote");
    if (result.remote.empty()) result.remote = config("remote.pushDefault");
    if (result.remote.empty()) result.remote = trackingRemote;
    if (result.remote == ".") result.remote.clear();
    const auto mergeRef = config("branch." + result.branch + ".merge");
    result.destination = mergeRef.starts_with("refs/heads/") ? mergeRef.substr(11) : result.branch;
    if (result.remote.empty()) { result.error = "Choose a remote and destination branch"; return result; }
    auto fetch = git_run(repository, {"remote", "get-url", "--all", result.remote}, stop);
    auto push = git_run(repository, {"remote", "get-url", "--push", "--all", result.remote}, stop);
    if (!fetch.success() || !push.success()) { result.error = "Unable to read this remote's URLs"; return result; }
    result.pushIdentity = storage::key(push.stdout_str());
    result.fetchUrl = redact_log_text(catalog::trim_line(fetch.stdout_str()));
    result.pushUrl = redact_log_text(catalog::trim_line(push.stdout_str()));
    return result;
}

inline GitResult push_explicit(const std::string& repository, const PushDestination& destination) {
    auto branch = git_run(repository, {"symbolic-ref", "--quiet", "--short", "HEAD"});
    if (!branch.success() || catalog::trim_line(branch.stdout_str()) != destination.branch)
        return GitResult{{"", "The checked-out branch changed; reopen Push", -1}};
    auto valid = git_run(repository, {"check-ref-format", "refs/heads/" + destination.destination});
    if (!valid.success()) return GitResult{{"", "Invalid destination branch", -1}};
    if (destination.remote.empty()) return GitResult{{"", "Choose a remote", -1}};
    auto current = read_push_destination(repository, destination.remote, {});
    if (!current.error.empty() || current.pushIdentity != destination.pushIdentity)
        return GitResult{{"", "The push destination changed; reopen Push", -1}};
    return git_run(repository, {"push", "--", destination.remote,
        "refs/heads/" + destination.branch + ":refs/heads/" + destination.destination});
}

}
