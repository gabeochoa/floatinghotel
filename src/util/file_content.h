#pragma once

#include <filesystem>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stop_token>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <string_view>
#include <functional>
#include <algorithm>

namespace file_content {

struct Read {
    std::string bytes;
    std::string mode;
    std::string error;
    uint64_t totalBytes = 0;
    std::string identity;
};

inline std::string identity_for_stat(const struct stat& status) {
#ifdef __APPLE__
    auto modified = status.st_mtimespec;
#else
    auto modified = status.st_mtim;
#endif
    return std::to_string(status.st_dev) + ":" + std::to_string(status.st_ino) + ":" +
        std::to_string(status.st_size) + ":" + std::to_string(modified.tv_sec) + ":" + std::to_string(modified.tv_nsec);
}

inline Read read_working_file(const std::filesystem::path& path, std::stop_token stop = {},
                              size_t maxBytes = std::numeric_limits<size_t>::max(),
                              std::function<bool(std::string_view)> consumeOutput = {}, uint64_t offset = 0) {
    Read out;
    if (stop.stop_requested()) { out.error = "File load cancelled"; return out; }
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    if (error) { out.error = error.message(); return out; }
    if (std::filesystem::is_symlink(status)) {
        auto target = std::filesystem::read_symlink(path, error);
        if (error) out.error = error.message();
        else {
            out.bytes = target.string(); out.mode = "120000"; out.totalBytes = out.bytes.size();
            if (consumeOutput) { consumeOutput(std::string_view(out.bytes).substr(std::min<uint64_t>(offset, out.bytes.size()))); out.bytes.clear(); }
        }
        return out;
    }
    if (!std::filesystem::is_regular_file(status)) {
        out.error = "Not a regular file or symbolic link";
        return out;
    }
    int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) { out.error = std::string("Unable to read working-tree file: ") + strerror(errno); return out; }
    struct stat opened{};
    if (fstat(descriptor, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        close(descriptor);
        out.error = "Not a regular file";
        return out;
    }
    out.mode = (opened.st_mode & S_IXUSR) ? "100755" : "100644";
    out.totalBytes = opened.st_size < 0 ? 0 : static_cast<uint64_t>(opened.st_size);
    out.identity = identity_for_stat(opened);
    if (opened.st_size < 0 || static_cast<uintmax_t>(opened.st_size) > maxBytes) {
        close(descriptor);
        out.error = "File exceeds the read size limit";
        return out;
    }
    if (offset > out.totalBytes || lseek(descriptor, static_cast<off_t>(offset), SEEK_SET) < 0) {
        close(descriptor); out.error = "Unable to seek to file page"; return out;
    }
    std::array<char, 65536> buffer;
    while (!stop.stop_requested()) {
        auto count = read(descriptor, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            out.error = "Unable to finish reading working-tree file";
            break;
        }
        if (consumeOutput) {
            if (!consumeOutput(std::string_view(buffer.data(), static_cast<size_t>(count)))) break;
        } else if (static_cast<size_t>(count) > maxBytes - out.bytes.size()) {
            out.error = "File exceeds the read size limit";
            break;
        } else out.bytes.append(buffer.data(), static_cast<size_t>(count));
    }
    struct stat completed{};
    if (fstat(descriptor, &completed) != 0 || identity_for_stat(completed) != out.identity)
        out.error = "File changed while reading; reload from the beginning";
    close(descriptor);
    if (stop.stop_requested()) out.error = "File load cancelled";
    return out;
}

inline std::string mode_for_path(const std::string& records, const std::string& path) {
    size_t start = 0;
    while (start < records.size()) {
        auto end = records.find('\0', start);
        if (end == std::string::npos) break;
        auto tab = records.find('\t', start);
        if (tab < end && records.substr(tab + 1, end - tab - 1) == path && tab - start >= 6)
            return records.substr(start, 6);
        start = end + 1;
    }
    return {};
}

}
