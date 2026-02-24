#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace git_helpers {

// Parse ISO 8601 date string (e.g. "2026-02-17T14:30:00+05:00") to time_t (UTC).
inline std::time_t parse_iso8601(const std::string& dateStr) {
    if (dateStr.size() < 19) return 0;
    std::tm tm{};
    tm.tm_year = std::stoi(dateStr.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(dateStr.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(dateStr.substr(8, 2));
    tm.tm_hour = std::stoi(dateStr.substr(11, 2));
    tm.tm_min  = std::stoi(dateStr.substr(14, 2));
    tm.tm_sec  = std::stoi(dateStr.substr(17, 2));
    tm.tm_isdst = -1;
    std::time_t t = timegm(&tm);
    if (dateStr.size() > 19) {
        char sign = dateStr[19];
        if (sign == '+' || sign == '-') {
            int tzH = std::stoi(dateStr.substr(20, 2));
            int tzM = (dateStr.size() >= 25) ? std::stoi(dateStr.substr(23, 2)) : 0;
            int offset = tzH * 3600 + tzM * 60;
            t += (sign == '+') ? -offset : offset;
        }
    }
    return t;
}

// Human-readable relative time from ISO 8601 date.
// When suffix is true, appends " ago" (e.g. "3d ago" vs "3d").
inline std::string relative_time(const std::string& isoDate, bool suffix = false) {
    if (isoDate.empty()) return "";
    std::time_t commitTime = parse_iso8601(isoDate);
    if (commitTime == 0) return "";
    std::time_t now = std::time(nullptr);
    long diff = static_cast<long>(std::difftime(now, commitTime));
    std::string s = suffix ? " ago" : "";
    if (diff < 0) return "now";
    if (diff < 60) return std::to_string(diff) + "s" + s;
    if (diff < 3600) return std::to_string(diff / 60) + "m" + s;
    if (diff < 86400) return std::to_string(diff / 3600) + "h" + s;
    if (diff < 604800) return std::to_string(diff / 86400) + "d" + s;
    if (diff < 2592000) return std::to_string(diff / 604800) + "w" + s;
    if (diff < 31536000) return std::to_string(diff / 2592000) + "mo" + s;
    return std::to_string(diff / 31536000) + "y" + s;
}

enum class DecorationType { LocalBranch, Head, RemoteBranch, Tag };

struct Decoration {
    std::string label;
    DecorationType type;
};

// Parse git log %D decoration string into typed badges.
inline std::vector<Decoration> parse_decorations(const std::string& decStr) {
    std::vector<Decoration> result;
    if (decStr.empty()) return result;
    std::string remaining = decStr;
    while (!remaining.empty()) {
        size_t pos = remaining.find(", ");
        std::string item;
        if (pos != std::string::npos) {
            item = remaining.substr(0, pos);
            remaining = remaining.substr(pos + 2);
        } else {
            item = remaining;
            remaining.clear();
        }
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;
        if (item.find("HEAD -> ") == 0) {
            result.push_back({"HEAD", DecorationType::Head});
            result.push_back({item.substr(8), DecorationType::LocalBranch});
        } else if (item == "HEAD") {
            result.push_back({"HEAD", DecorationType::Head});
        } else if (item.find("tag: ") == 0) {
            result.push_back({item.substr(5), DecorationType::Tag});
        } else if (item.find('/') != std::string::npos) {
            result.push_back({item, DecorationType::RemoteBranch});
        } else {
            result.push_back({item, DecorationType::LocalBranch});
        }
    }
    return result;
}

} // namespace git_helpers
