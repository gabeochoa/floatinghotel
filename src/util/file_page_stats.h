#pragma once

#include "file_page.h"

namespace file_page {

struct Stats {
    size_t rawBytes = 0;
    size_t decodedBytes = 0;
    size_t lineBytes = 0;
    size_t lines = 0;

    bool bounded(size_t pages = 1) const {
        return rawBytes <= pages * byteLimit && decodedBytes <= pages * 3 * byteLimit &&
            lineBytes <= pages * (3 * byteLimit + static_cast<size_t>(lineLimit)) &&
            lines <= pages * static_cast<size_t>(lineLimit);
    }
};

inline Stats stats(std::string_view raw, const std::vector<ecs::FileDiff>& files,
                   std::string_view decoded = {}) {
    Stats result{raw.size(), decoded.size()};
    for (const auto& file : files)
        for (const auto& hunk : file.hunks) {
            result.lines += hunk.lines.size();
            for (const auto& line : hunk.lines) result.lineBytes += line.size();
        }
    return result;
}

}
