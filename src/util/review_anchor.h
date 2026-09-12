#pragma once

#include <charconv>
#include <sstream>

#include "../ecs/components.h"

namespace review_anchor {

enum class Status { Current, Relocated, Outdated, Unknown };

struct Result {
    Status status = Status::Unknown;
    int line = 0;
    std::string saved;
    std::string current;
};

inline std::string label(const Result& result) {
    switch (result.status) {
        case Status::Current: return "Current";
        case Status::Relocated: return "Moved to line " + std::to_string(result.line);
        case Status::Outdated: return "Outdated";
        case Status::Unknown: return "Unknown: outside loaded patch evidence";
    }
    return "Unknown";
}

inline Result locate(const ecs::ReviewComponent::Comment& comment, const std::vector<ecs::FileDiff>* files) {
    Result result;
    std::map<int, std::string> saved;
    std::istringstream excerpt(comment.codeContext);
    std::string line;
    int last = std::max(comment.line, comment.endLine);
    while (std::getline(excerpt, line)) {
        auto colon = line.find(": ");
        if (colon == std::string::npos) continue;
        int number = 0;
        auto parsed = std::from_chars(line.data(), line.data() + colon, number);
        if (parsed.ec == std::errc{} && parsed.ptr == line.data() + colon && number >= comment.line && number <= last)
            saved[number] = line.substr(colon + 2);
    }
    if (!files || comment.line < 1 || saved.size() != static_cast<size_t>(last - comment.line + 1)) return result;
    for (const auto& [number, text] : saved) {
        if (!result.saved.empty()) result.saved += " | ";
        result.saved += text;
    }
    auto file = std::find_if(files->begin(), files->end(), [&](const auto& candidate) {
        return candidate.filePath == comment.file || (comment.oldSide && candidate.oldPath == comment.file);
    });
    if (file == files->end()) return result;
    if (file->isDeleted && !comment.oldSide) {
        result.status = Status::Outdated;
        result.current = "File deleted";
        return result;
    }
    std::map<int, std::string> current;
    for (const auto& hunk : file->hunks) {
        int oldLine = hunk.oldStart, newLine = hunk.newStart;
        for (const auto& text : hunk.lines) {
            char sign = text.empty() ? ' ' : text.front();
            if (comment.oldSide ? sign != '+' : sign != '-')
                current[comment.oldSide ? oldLine : newLine] = text.empty() ? "" : text.substr(1);
            if (sign != '+') ++oldLine;
            if (sign != '-') ++newLine;
        }
    }
    auto matches = [&](int start) {
        for (const auto& [number, text] : saved) {
            auto found = current.find(start + number - comment.line);
            if (found == current.end() || found->second != text) return false;
        }
        return true;
    };
    if (matches(comment.line)) {
        result.status = Status::Current;
        result.line = comment.line;
        result.current = result.saved;
        return result;
    }
    std::vector<int> candidates;
    for (const auto& [number, text] : current)
        if (text == saved.begin()->second && matches(number)) candidates.push_back(number);
    if (candidates.size() == 1) {
        result.status = Status::Relocated;
        result.line = candidates.front();
        result.current = result.saved;
        return result;
    }
    if (!candidates.empty()) return result;
    for (const auto& [number, text] : saved) {
        auto found = current.find(number);
        if (found == current.end()) return result;
        if (!result.current.empty()) result.current += " | ";
        result.current += found->second;
    }
    result.status = Status::Outdated;
    return result;
}

inline std::string preview(std::string text) {
    if (text.size() > 160) text = text.substr(0, 160) + "...";
    return text;
}

}
