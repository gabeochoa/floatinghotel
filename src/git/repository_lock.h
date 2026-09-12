#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace git {

inline std::string repository_lock_key(const std::string& repo) {
    namespace fs = std::filesystem;
    std::error_code error;
    auto path = fs::weakly_canonical(repo.empty() ? fs::current_path(error) : fs::path(repo), error);
    if (error) return repo;
    const auto fallback = path;
    for (;;) {
        fs::path directory;
        const auto marker = path / ".git";
        if (fs::is_directory(marker, error)) directory = marker;
        else if (fs::is_regular_file(marker, error)) {
            std::ifstream input(marker);
            std::string line;
            std::getline(input, line);
            if (line.starts_with("gitdir: ")) {
                line.erase(0, 8);
                if (line.ends_with('\r')) line.pop_back();
                directory = fs::path(line);
                if (directory.is_relative()) directory = path / directory;
            }
        } else if (fs::is_regular_file(path / "HEAD", error) &&
                   (fs::is_directory(path / "objects", error) || fs::is_regular_file(path / "commondir", error))) {
            directory = path;
        }
        if (!directory.empty()) {
            std::ifstream common(directory / "commondir");
            std::string line;
            if (std::getline(common, line) && !line.empty()) {
                if (line.ends_with('\r')) line.pop_back();
                auto shared = fs::path(line);
                directory = shared.is_absolute() ? shared : directory / shared;
            }
            auto canonical = fs::weakly_canonical(directory, error);
            return error ? directory.lexically_normal().string() : canonical.string();
        }
        if (path == path.parent_path()) break;
        path = path.parent_path();
    }
    return fallback.string();
}

inline std::shared_ptr<std::shared_timed_mutex> repository_mutex(const std::string& repo) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::weak_ptr<std::shared_timed_mutex>> locks;
    auto key = repository_lock_key(repo);
    std::lock_guard guard(mutex);
    for (auto it = locks.begin(); it != locks.end();)
        if (it->second.expired()) it = locks.erase(it); else ++it;
    if (auto existing = locks[key].lock()) return existing;
    auto created = std::make_shared<std::shared_timed_mutex>();
    locks[key] = created;
    return created;
}

}
