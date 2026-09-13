#pragma once

#include "../ecs/components.h"
#include "text_decode.h"
#include <string_view>
#include <limits>

namespace file_page {

inline constexpr size_t byteLimit = 256 * 1024;
inline constexpr int lineLimit = 4096;

inline text_decode::Result decode(std::string_view bytes, const std::string& encoding, uint64_t offset) {
    if (offset == 0) return text_decode::decode(bytes, encoding);
    if (encoding == "utf16le" || encoding == "utf16be") {
        auto [text, malformed] = text_decode::utf16_to_utf8(bytes, encoding == "utf16le", 0);
        bool binary = text.find('\0') != std::string::npos;
        return {std::move(text), text_decode::label(encoding == "utf16le" ? "UTF-16 LE" : "UTF-16 BE", malformed), binary, malformed};
    }
    auto [text, malformed] = text_decode::utf8_clean(bytes, 0);
    bool binary = text.find('\0') != std::string::npos;
    return {std::move(text), text_decode::label("UTF-8", malformed), binary, malformed};
}

class Collector {
    ecs::FilePageRequest request_;
    std::string pending_;
    uint64_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
    int pageLines_ = 0;
    bool continuation_ = false;
    bool finished_ = false;
    bool reachedTarget_ = false;

    size_t unit_size(size_t at, bool eof) const {
        auto left = pending_.size() - at;
        auto byte = [&](size_t index) { return static_cast<unsigned char>(pending_[index]); };
        if (encoding == "utf16le" || encoding == "utf16be") {
            if (left < 2) return eof ? left : 0;
            auto unit = [&](size_t index) { return encoding == "utf16le" ? byte(index) | (byte(index + 1) << 8) : (byte(index) << 8) | byte(index + 1); };
            if (unit(at) >= 0xd800 && unit(at) <= 0xdbff) {
                if (left < 4 && !eof) return 0;
                if (left >= 4 && unit(at + 2) >= 0xdc00 && unit(at + 2) <= 0xdfff) return 4;
            }
            return 2;
        }
        auto c = byte(at);
        size_t size = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 1;
        for (size_t i = 1; i < std::min(size, left); ++i)
            if ((byte(at + i) & 0xc0) != 0x80) return 1;
        if (left >= size && ((size == 3 && ((c == 0xe0 && byte(at + 1) < 0xa0) || (c == 0xed && byte(at + 1) >= 0xa0))) ||
            (size == 4 && ((c == 0xf0 && byte(at + 1) < 0x90) || (c == 0xf4 && byte(at + 1) >= 0x90))))) return 1;
        return left < size ? (eof ? 1 : 0) : size;
    }

    bool process(bool eof) {
        if (encoding.empty()) {
            if (pending_.size() < 512 && !eof) return true;
            auto detected = text_decode::decode(std::string_view(pending_).substr(0, 512)).encoding;
            encoding = detected.starts_with("UTF-16 LE") ? "utf16le" : detected.starts_with("UTF-16 BE") ? "utf16be" : "utf8";
        }
        size_t at = 0;
        while (at < pending_.size()) {
            if (request_.action == ecs::FilePageRequest::Action::Previous && position_ >= request_.cursor.offset) {
                finished_ = true;
                break;
            }
            auto size = unit_size(at, eof);
            if (size == 0) break;
            bool newline = encoding == "utf16le" ? size == 2 && pending_[at] == '\n' && pending_[at + 1] == '\0' :
                encoding == "utf16be" ? size == 2 && pending_[at] == '\0' && pending_[at + 1] == '\n' : size == 1 && pending_[at] == '\n';
            if (request_.action == ecs::FilePageRequest::Action::Next && position_ < request_.cursor.offset) {
                position_ += size;
                at += size;
                continue;
            }
            const bool target = request_.action == ecs::FilePageRequest::Action::TargetLine;
            const int firstLine = request_.targetLine - std::clamp(request_.leadingLines, 0, lineLimit / 2);
            bool collecting = !target || line_ >= firstLine;
            if (target && (line_ < request_.targetLine || (line_ == request_.targetLine && column_ < request_.targetColumn)) && raw.size() + size > byteLimit / 2) {
                raw.clear();
                pageLines_ = 0;
            }
            if (collecting && (raw.size() + size > byteLimit || pageLines_ >= lineLimit)) {
                if (request_.action == ecs::FilePageRequest::Action::Previous) { raw.clear(); pageLines_ = 0; }
                else { finished_ = true; break; }
            }
            if (collecting) {
                reachedTarget_ |= line_ >= request_.targetLine;
                if (raw.empty()) begin = {position_, line_, continuation_, column_};
                raw.append(pending_, at, size);
                if (newline) ++pageLines_;
            }
            const bool bom = position_ == 0 && ((encoding == "utf8" && pending_.substr(at, size) == "\xef\xbb\xbf") ||
                (encoding == "utf16le" && pending_.substr(at, size) == "\xff\xfe") ||
                (encoding == "utf16be" && pending_.substr(at, size) == "\xfe\xff"));
            position_ += size;
            at += size;
            if (newline) {
                column_ = 1;
                if (line_ == std::numeric_limits<int>::max()) { error = "File exceeds supported line-number range"; finished_ = true; break; }
                ++line_;
            } else if (!bom) {
                if (column_ == std::numeric_limits<int>::max()) { error = "File exceeds supported column-number range"; finished_ = true; break; }
                ++column_;
            }
            continuation_ = !newline;
            next = {position_, line_, continuation_, column_};
        }
        pending_.erase(0, at);
        return !finished_;
    }
public:
    std::string raw;
    std::string encoding;
    std::string error;
    ecs::FilePageCursor begin;
    ecs::FilePageCursor next;

    Collector(ecs::FilePageRequest request, std::string overrideEncoding, const std::string& detected, uint64_t sourceOffset = 0)
        : request_(request), position_(sourceOffset),
          line_(request.action == ecs::FilePageRequest::Action::Next ? request.cursor.line : 1),
          column_(request.action == ecs::FilePageRequest::Action::Next ? request.cursor.column : 1),
          continuation_(request.action == ecs::FilePageRequest::Action::Next && request.cursor.continuation),
          encoding(overrideEncoding == "auto" ? detected : std::move(overrideEncoding)) {
        raw.reserve(byteLimit);
        if (request.action == ecs::FilePageRequest::Action::Next) begin = next = request.cursor;
    }

    bool consume(std::string_view chunk) {
        if (finished_) return false;
        if (!encoding.empty() && pending_.empty() && request_.action == ecs::FilePageRequest::Action::Next && position_ < request_.cursor.offset) {
            auto skip = std::min<uint64_t>(chunk.size(), request_.cursor.offset - position_);
            position_ += skip;
            chunk.remove_prefix(static_cast<size_t>(skip));
        }
        pending_.append(chunk);
        return process(false);
    }

    void finish() {
        if (!finished_) process(true);
        if (request_.action == ecs::FilePageRequest::Action::TargetLine && !reachedTarget_)
            error = "Requested line is beyond the end of this file";
        raw.shrink_to_fit();
    }
};

}
