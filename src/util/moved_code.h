#pragma once

#include "../ecs/components.h"
#include <unordered_map>

namespace moved_code {

inline void mark_blocks(std::vector<ecs::FileDiff>& files) {
    struct Block { ecs::DiffHunk* hunk; size_t first; size_t end; };
    struct Occurrences { std::vector<Block> before; std::vector<Block> after; };
    std::unordered_map<std::string, Occurrences> blocks;
    for (auto& file : files) {
        for (auto& hunk : file.hunks) {
            hunk.movedLines.clear();
            size_t i = 0;
            while (i < hunk.lines.size()) {
                char sign = hunk.lines[i].empty() ? ' ' : hunk.lines[i].front();
                if (sign != '+' && sign != '-') { ++i; continue; }
                size_t first = i;
                std::string content;
                while (i < hunk.lines.size() && !hunk.lines[i].empty() && hunk.lines[i].front() == sign) {
                    content += hunk.lines[i].substr(1);
                    if (!hunk.noNewline.contains(i)) content += '\n';
                    ++i;
                }
                if (i - first < 2 || content.size() < 20) continue;
                auto& occurrences = blocks[content];
                (sign == '-' ? occurrences.before : occurrences.after).push_back({&hunk, first, i});
            }
        }
    }
    for (const auto& [content, occurrences] : blocks) {
        if (occurrences.before.size() != 1 || occurrences.after.size() != 1) continue;
        for (const auto& block : {occurrences.before.front(), occurrences.after.front()})
            for (size_t i = block.first; i < block.end; ++i) block.hunk->movedLines.insert(i);
    }
}

}
