#pragma once

#include <charconv>
#include <sstream>

#include "../ecs/components.h"

namespace review_anchor {

enum class Status { Current, Relocated, Outdated, Ambiguous, Unknown };

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
        case Status::Ambiguous: return "Ambiguous: multiple matching locations";
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
    if (!candidates.empty()) { result.status = Status::Ambiguous; return result; }
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

namespace review_anchor {

struct Carried { size_t moved = 0, ambiguous = 0; };

// After a commit, moves unresolved working-tree/index comments whose saved
// lines are changed lines of `commit` (matched by content, so earlier edits
// in the file don't matter) onto the commit, re-anchored to its line numbers
// with a fresh excerpt. Ambiguous matches stay put and are counted.
// ponytail: multi-range comments stay put; shift each range when needed.
inline Carried carry_to_commit(std::vector<ecs::ReviewComponent::Comment>& comments,
    const std::string& commit, const std::vector<ecs::FileDiff>& files) {
    Carried carried;
    for (auto& comment : comments) {
        if (comment.resolved || (comment.scope != "wt" && comment.scope != "index") || comment.ranges.size() > 1) continue;
        const auto anchor = locate(comment, &files);
        if (anchor.status == Status::Ambiguous) ++carried.ambiguous;
        if (anchor.status != Status::Current && anchor.status != Status::Relocated) continue;
        const int span = std::max(0, comment.endLine - comment.line);
        const auto file = std::find_if(files.begin(), files.end(), [&](const auto& candidate) {
            return candidate.filePath == comment.file || (comment.oldSide && candidate.oldPath == comment.file);
        });
        for (const auto& hunk : file->hunks) {
            const int first = comment.oldSide ? hunk.oldStart : hunk.newStart;
            const int count = comment.oldSide ? hunk.oldCount : hunk.newCount;
            if (anchor.line < first || anchor.line + span >= first + count) continue;
            bool changed = false;
            int number = first;
            for (const auto& text : hunk.lines) {
                const char sign = text.empty() ? ' ' : text.front();
                if (sign == (comment.oldSide ? '+' : '-')) continue;
                changed |= sign != ' ' && number >= anchor.line && number <= anchor.line + span;
                ++number;
            }
            if (!changed) break;
            auto moved = comment;
            moved.scope = commit;
            moved.file = comment.oldSide ? file->oldPath : file->filePath;
            if (moved.file.empty()) moved.file = file->filePath;
            moved.line = anchor.line;
            moved.endLine = comment.endLine ? anchor.line + span : 0;
            for (auto& range : moved.ranges) range = {anchor.line, anchor.line + span, comment.oldSide};
            moved.codeContext.clear();
            comment = ecs::comment_with_context(std::move(moved), hunk, "");
            ++carried.moved;
            break;
        }
    }
    return carried;
}

}
