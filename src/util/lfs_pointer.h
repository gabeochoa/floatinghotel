#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace lfs_pointer {

struct Pointer { std::string oid; std::uint64_t size = 0; };

inline std::optional<Pointer> parse(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line)) return std::nullopt;
    if (line.ends_with('\r')) line.pop_back();
    if (line != "version https://git-lfs.github.com/spec/v1") return std::nullopt;
    Pointer result;
    bool hasSize = false;
    while (std::getline(lines, line)) {
        if (line.ends_with('\r')) line.pop_back();
        if (line.starts_with("oid sha256:") && result.oid.empty()) {
            result.oid = line.substr(11);
            if (result.oid.size() != 64 || result.oid.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
        } else if (line.starts_with("size ") && !hasSize) {
            auto number = std::from_chars(line.data() + 5, line.data() + line.size(), result.size);
            if (number.ec != std::errc{} || number.ptr != line.data() + line.size()) return std::nullopt;
            hasSize = true;
        } else if (!line.empty() && !line.starts_with("ext-")) return std::nullopt;
    }
    if (result.oid.empty() || !hasSize) return std::nullopt;
    return result;
}

inline std::filesystem::path common_git_directory(const std::filesystem::path& repo) {
    auto directory = repo / ".git";
    std::error_code error;
    if (std::filesystem::is_regular_file(directory, error)) {
        std::ifstream file(directory);
        std::string line;
        if (!std::getline(file, line) || !line.starts_with("gitdir: ")) return {};
        directory = std::filesystem::path(line.substr(8));
        if (directory.is_relative()) directory = repo / directory;
    }
    std::ifstream common(directory / "commondir");
    std::string relative;
    if (std::getline(common, relative)) directory = directory / relative;
    return directory;
}

inline bool locally_available(const std::filesystem::path& repo, const Pointer& pointer) {
    auto directory = common_git_directory(repo);
    if (directory.empty()) return false;
    auto path = directory / "lfs" / "objects" / pointer.oid.substr(0, 2) / pointer.oid.substr(2, 2) / pointer.oid;
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && std::filesystem::file_size(path, error) == pointer.size && !error;
}

inline bool cached_availability(const std::string& repo, const Pointer& pointer, unsigned generation) {
    static std::unordered_map<std::string, std::pair<unsigned, bool>> cache;
    std::string key = repo + "\n" + pointer.oid + "\n" + std::to_string(pointer.size);
    if (auto it = cache.find(key); it != cache.end() && it->second.first == generation) return it->second.second;
    if (cache.size() >= 256) cache.clear();
    bool available = locally_available(repo, pointer);
    cache[key] = {generation, available};
    return available;
}

}
