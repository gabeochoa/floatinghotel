#include "review_snapshot.h"

#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <set>
#include <thread>
#include <nlohmann/json.hpp>
#include <afterhours/src/plugins/files.h>

namespace review_store {
namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr size_t snapshotLimit = 32 * 1024 * 1024;

std::set<std::string> changed_paths(const std::string& repo, const std::string& head) {
    std::set<std::string> paths;
    for (const auto& args : std::vector<std::vector<std::string>>{
             head.empty() ? std::vector<std::string>{"ls-files", "-z"} :
                 std::vector<std::string>{"diff", "--name-only", "--no-renames", "-z", head, "--"},
             {"ls-files", "--others", "--exclude-standard", "-z"}}) {
        auto result = git::git_run(repo, args);
        if (!result.success()) throw std::runtime_error(result.stderr_str());
        const auto& data = result.stdout_str();
        for (size_t start = 0, end; start < data.size(); start = end + 1) {
            end = data.find('\0', start);
            if (end == std::string::npos) end = data.size();
            paths.insert(data.substr(start, end - start));
        }
    }
    return paths;
}

nlohmann::json read_working_file(const std::string& repo, const std::string& file, size_t& total) {
    auto relative = std::filesystem::path(file);
    if (relative.is_absolute() || relative.lexically_normal() != relative || file.starts_with("../"))
        throw std::runtime_error("Invalid snapshot path");
    auto path = std::filesystem::path(repo) / relative;
    auto status = std::filesystem::symlink_status(path);
    if (status.type() == std::filesystem::file_type::not_found) return nullptr;
    if (!std::filesystem::is_regular_file(status))
        throw std::runtime_error("Snapshots require regular files: " + file);
    auto size = std::filesystem::file_size(path);
    if (size > snapshotLimit || total + size > snapshotLimit)
        throw std::runtime_error("Review snapshot exceeds the 32 MiB limit");
    total += size;
    std::ifstream input(path, std::ios::binary);
    Bytes bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read " + file);
    return nlohmann::json::binary(bytes);
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "floatinghotel-snapshot-XXXXXX").string();
        auto* created = mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("Cannot create snapshot comparison directory");
        path = created;
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

git::GitResult snapshot(const std::string& repo, const std::string& path, const std::string& head,
                         bool capture, int context, bool ignoreWhitespace) {
    try {
        size_t total = 0;
        if (capture) {
            const auto captured = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            nlohmann::json saved = {{"head", head}, {"captured_at", captured}, {"files", nlohmann::json::object()}};
            for (const auto& file : changed_paths(repo, head)) saved["files"][file] = read_working_file(repo, file, total);
            auto encoded = nlohmann::json::to_cbor(saved);
            if (!afterhours::files::write_string_atomic(path, std::string(encoded.begin(), encoded.end())))
                throw std::runtime_error("Cannot save review snapshot");
            return {{nlohmann::json{{"path", path}, {"head", head}, {"captured_at", captured}}.dump(), "", 0}};
        }
        auto encoded = afterhours::files::read_string(path);
        if (!encoded || encoded->size() > snapshotLimit + 1024 * 1024)
            throw std::runtime_error("Review snapshot is missing or too large. Capture a new baseline.");
        auto saved = nlohmann::json::from_cbor(*encoded);
        auto baseline = saved.at("head").get<std::string>();
        auto paths = changed_paths(repo, baseline);
        for (const auto& [file, bytes] : saved.at("files").items()) paths.insert(file);
        TemporaryDirectory temporary;
        nlohmann::json patches = nlohmann::json::array();
        for (const auto& file : paths) {
            auto after = read_working_file(repo, file, total);
            nlohmann::json before;
            if (saved["files"].contains(file)) before = saved["files"][file];
            else if (!baseline.empty()) {
                auto exists = git::git_run(repo, {"ls-tree", "-z", baseline, "--", ":(literal)" + file});
                if (!exists.success()) throw std::runtime_error(exists.stderr_str());
                if (!exists.stdout_str().empty()) {
                    auto content = git::git_run(repo, {"show", baseline + ":" + file});
                    if (!content.success()) throw std::runtime_error(content.stderr_str());
                    if (content.stdout_str().size() > snapshotLimit) throw std::runtime_error("Baseline file exceeds 32 MiB");
                    before = nlohmann::json::binary(Bytes(content.stdout_str().begin(), content.stdout_str().end()));
                }
            }
            if (before == after) continue;
            for (const auto& [name, bytes] : std::vector<std::pair<std::string, nlohmann::json>>{{"before", before}, {"after", after}}) {
                std::ofstream output(temporary.path / name, std::ios::binary);
                if (!bytes.is_null()) {
                    const auto& data = bytes.get_binary();
                    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                }
                if (!output) throw std::runtime_error("Cannot write snapshot comparison file");
            }
            std::vector<std::string> args{"diff", "--no-index", "--no-ext-diff", "--no-color", "--unified=" + std::to_string(context)};
            if (ignoreWhitespace) args.push_back("--ignore-all-space");
            args.insert(args.end(), {"--", before.is_null() ? "/dev/null" : "before", after.is_null() ? "/dev/null" : "after"});
            auto patch = git::git_run(temporary.path.string(), args);
            if (patch.exit_code() > 1 || patch.exit_code() < 0) throw std::runtime_error(patch.stderr_str());
            patches.push_back({{"file", file}, {"patch", patch.stdout_str()}});
        }
        return {{patches.dump(), "", 0}};
    } catch (const std::exception& error) { return {{"", error.what(), 1}}; }
}

}

async_work::Task<git::GitResult> snapshot_async(std::string repo, std::string path,
    std::string head, bool capture, int context, bool ignoreWhitespace) {
    return async_work::launch([=](std::stop_token) { return snapshot(repo, path, head, capture, context, ignoreWhitespace); },
        async_work::Priority::Foreground, git::GitResult{{"", "Background queue is full; retry snapshot", -1}}, false);
}

}
