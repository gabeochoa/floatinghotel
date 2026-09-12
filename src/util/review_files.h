#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace review_files {

struct Filter {
    bool hideGenerated = false;
    bool hideVendor = false;
    bool hideLockfiles = false;
};

inline bool matches(const Filter& filter, std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto name = std::string_view(path).substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    auto segment = [&](std::string_view part) {
        return path.starts_with(std::string(part) + "/") || path.find("/" + std::string(part) + "/") != std::string::npos;
    };
    if (filter.hideVendor && (segment("vendor") || segment("node_modules") || segment("third_party"))) return false;
    if (filter.hideGenerated && (segment("generated") || segment("dist") || name.find(".generated.") != std::string_view::npos ||
        name.ends_with(".min.js") || name.ends_with(".min.css") || name.ends_with(".pb.cc") || name.ends_with(".pb.h"))) return false;
    if (filter.hideLockfiles && (name.ends_with(".lock") || name == "package-lock.json" || name == "pnpm-lock.yaml" ||
        name == "go.sum" || name == "npm-shrinkwrap.json")) return false;
    return true;
}

}
