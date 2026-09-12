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

namespace file_content {

struct Read {
    std::string bytes;
    std::string mode;
    std::string error;
};

inline Read read_working_file(const std::filesystem::path& path, std::stop_token stop = {},
                              size_t maxBytes = std::numeric_limits<size_t>::max()) {
    Read out;
    if (stop.stop_requested()) { out.error = "File load cancelled"; return out; }
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    if (error) { out.error = error.message(); return out; }
    if (std::filesystem::is_symlink(status)) {
        auto target = std::filesystem::read_symlink(path, error);
        if (error) out.error = error.message();
        else { out.bytes = target.string(); out.mode = "120000"; }
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
    if (opened.st_size < 0 || static_cast<uintmax_t>(opened.st_size) > maxBytes) {
        close(descriptor);
        out.error = "File exceeds the read size limit";
        return out;
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
        if (static_cast<size_t>(count) > maxBytes - out.bytes.size()) {
            out.error = "File exceeds the read size limit";
            break;
        }
        out.bytes.append(buffer.data(), static_cast<size_t>(count));
    }
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
