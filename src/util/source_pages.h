#pragma once

#include "file_page.h"

namespace source_pages {

inline constexpr size_t pageLimit = 3;

inline bool bounded(const ecs::SourcePageWindow& window) {
    if (window.pages.size() > pageLimit || window.raw.size() > pageLimit * file_page::byteLimit) return false;
    size_t bytes = 0;
    for (size_t i = 0; i < window.pages.size(); ++i) {
        const auto& page = window.pages[i];
        if (page.next.offset < page.begin.offset || page.next.offset - page.begin.offset > file_page::byteLimit ||
            page.next.line < page.begin.line || page.next.line - page.begin.line > file_page::lineLimit) return false;
        if (i && (window.pages[i - 1].next.offset != page.begin.offset ||
            window.pages[i - 1].next.line != page.begin.line || window.pages[i - 1].next.column != page.begin.column ||
            window.pages[i - 1].next.lexical != page.begin.lexical ||
            window.pages.front().sourceIdentity != page.sourceIdentity || window.pages.front().encoding != page.encoding)) return false;
        bytes += static_cast<size_t>(page.next.offset - page.begin.offset);
    }
    return bytes == window.raw.size();
}

inline size_t owned_bytes(const ecs::SourcePageWindow& window) {
    size_t bytes = window.raw.capacity() + 1 + window.pages.capacity() * sizeof(ecs::FilePage);
    for (const auto& page : window.pages) bytes += page.blob.capacity() + page.encoding.capacity() + page.sourceIdentity.capacity() + 3;
    return bytes;
}

inline ecs::FilePage range(const ecs::SourcePageWindow& window) {
    if (window.pages.empty()) return {};
    auto page = window.pages.front();
    page.next = window.pages.back().next;
    return page;
}

inline bool extend(ecs::SourcePageWindow& window, ecs::FilePage page, std::string raw) {
    if (window.pages.empty() || raw.empty() || raw.size() > file_page::byteLimit ||
        page.next.offset < page.begin.offset || page.next.offset - page.begin.offset != raw.size() ||
        page.next.line < page.begin.line || page.next.line - page.begin.line > file_page::lineLimit) return false;
    const auto& first = window.pages.front();
    const auto& last = window.pages.back();
    if (page.sourceIdentity != first.sourceIdentity || page.encoding != first.encoding ||
        page.totalBytes != first.totalBytes || page.blob != first.blob) return false;
    const bool after = last.next.offset == page.begin.offset && last.next.line == page.begin.line && last.next.column == page.begin.column && last.next.lexical == page.begin.lexical;
    const bool before = page.next.offset == first.begin.offset && page.next.line == first.begin.line && page.next.column == first.begin.column && page.next.lexical == first.begin.lexical;
    if (!after && !before) return false;
    if (after) {
        if (window.pages.size() == pageLimit) {
            window.raw.erase(0, static_cast<size_t>(window.pages.front().next.offset - window.pages.front().begin.offset));
            window.pages.erase(window.pages.begin());
        }
        window.raw += raw;
        window.pages.push_back(std::move(page));
    } else {
        if (window.pages.size() == pageLimit) {
            window.raw.resize(window.raw.size() - static_cast<size_t>(window.pages.back().next.offset - window.pages.back().begin.offset));
            window.pages.pop_back();
        }
        window.raw.insert(0, raw);
        window.pages.insert(window.pages.begin(), std::move(page));
    }
    window.raw.shrink_to_fit();
    return true;
}

}
