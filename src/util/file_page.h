#pragma once

#include "../ecs/components.h"
#include "text_decode.h"
#include <cstring>
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
    enum class Enc : unsigned char { Unknown, Utf8, Utf16Le, Utf16Be };

    ecs::FilePageRequest request_;
    std::string pending_;
    uint64_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
    int pageLines_ = 0;
    bool continuation_ = false;
    bool finished_ = false;
    bool reachedTarget_ = false;
    Enc enc_ = Enc::Unknown;
    code_lexer::Language language_ = code_lexer::Language::Plain;
    code_lexer::State lexical_;

    static Enc classify_enc(const std::string& name) {
        if (name.empty()) return Enc::Unknown;
        if (name == "utf16le") return Enc::Utf16Le;
        if (name == "utf16be") return Enc::Utf16Be;
        return Enc::Utf8;
    }

    static bool is_code_trigger(unsigned char c) {
        switch (c) {
            case '\n': case '\r': case '/': case '*': case '\'':
            case '"': case '`': case '#': case '-': case 'R':
            case '\\': case ')':
                return true;
            default:
                return false;
        }
    }

    void advance_lexical(size_t at) {
        if (language_ == code_lexer::Language::Plain) return;
        if (enc_ == Enc::Utf8) {
            code_lexer::advance_with_lookahead(lexical_, language_, std::string_view(pending_).substr(at, code_lexer::lookaheadSize));
            return;
        }
        std::array<char, code_lexer::lookaheadSize> ahead{};
        size_t count = 0;
        for (size_t offset = at; offset + 1 < pending_.size() && count < ahead.size(); offset += 2) {
            const auto a = static_cast<unsigned char>(pending_[offset]);
            const auto b = static_cast<unsigned char>(pending_[offset + 1]);
            const auto unit = enc_ == Enc::Utf16Le ? a | (b << 8) : (a << 8) | b;
            ahead[count++] = unit <= 127 ? static_cast<char>(unit) : static_cast<char>(128);
        }
        code_lexer::advance_with_lookahead(lexical_, language_, {ahead.data(), count});
    }

    size_t unit_size(size_t at, bool eof) const {
        auto left = pending_.size() - at;
        auto byte = [&](size_t index) { return static_cast<unsigned char>(pending_[index]); };
        if (enc_ != Enc::Utf8) {
            if (left < 2) return eof ? left : 0;
            auto unit = [&](size_t index) { return enc_ == Enc::Utf16Le ? byte(index) | (byte(index + 1) << 8) : (byte(index) << 8) | byte(index + 1); };
            if (unit(at) >= 0xd800 && unit(at) <= 0xdbff) {
                if (left < 4 && !eof) return 0;
                if (left >= 4 && unit(at + 2) >= 0xdc00 && unit(at + 2) <= 0xdfff) return 4;
            }
            return 2;
        }
        auto c = byte(at);
        if (c <= 0x7f) return 1;
        size_t size = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 1;
        for (size_t i = 1; i < std::min(size, left); ++i)
            if ((byte(at + i) & 0xc0) != 0x80) return 1;
        if (left >= size && ((size == 3 && ((c == 0xe0 && byte(at + 1) < 0xa0) || (c == 0xed && byte(at + 1) >= 0xa0))) ||
            (size == 4 && ((c == 0xf0 && byte(at + 1) < 0x90) || (c == 0xf4 && byte(at + 1) >= 0x90))))) return 1;
        return left < size ? (eof ? 1 : 0) : size;
    }

    bool process(bool eof) {
        if (enc_ == Enc::Unknown) {
            if (pending_.size() < 512 && !eof) return true;
            auto detected = text_decode::decode(std::string_view(pending_).substr(0, 512)).encoding;
            encoding = detected.starts_with("UTF-16 LE") ? "utf16le" : detected.starts_with("UTF-16 BE") ? "utf16be" : "utf8";
            enc_ = classify_enc(encoding);
        }
        const char* data = pending_.data();
        const size_t total = pending_.size();
        size_t at = 0;
        const bool target = request_.action == ecs::FilePageRequest::Action::TargetLine;
        const int firstLine = target ? request_.targetLine - std::clamp(request_.leadingLines, 0, lineLimit / 2) : 1;

        // Fast-skip UTF-8 lines before TargetLine's collection window.
        if (target && enc_ == Enc::Utf8 && line_ < firstLine && at < total) {
            // Handle any BOM at byte 0 once before entering fast-skip.
            if (position_ == 0 && total >= 3 &&
                static_cast<unsigned char>(data[0]) == 0xef &&
                static_cast<unsigned char>(data[1]) == 0xbb &&
                static_cast<unsigned char>(data[2]) == 0xbf) {
                position_ += 3;
                at += 3;
            }
            if (language_ == code_lexer::Language::Plain) {
                while (line_ < firstLine && at < total) {
                    const void* found = std::memchr(data + at, '\n', total - at);
                    if (!found) {
                        const size_t rem = total - at;
                        position_ += rem;
                        at = total;
                        continuation_ = true;
                        break;
                    }
                    const size_t step = static_cast<size_t>(static_cast<const char*>(found) - (data + at)) + 1;
                    position_ += step;
                    at += step;
                    column_ = 1;
                    continuation_ = false;
                    if (line_ == std::numeric_limits<int>::max()) {
                        error = "File exceeds supported line-number range";
                        finished_ = true;
                        break;
                    }
                    ++line_;
                }
            } else {
                const size_t limit = (!eof && total > code_lexer::lookaheadSize * 2)
                    ? total - code_lexer::lookaheadSize * 2 : (eof ? total : 0);
                while (line_ < firstLine && at < limit) {
                    if (lexical_.skip == 0) {
                        if (lexical_.mode == code_lexer::Mode::BlockComment) {
                            // Inside /* ... */ only '*' or '\n' can change state or line count.
                            size_t scan = at;
                            while (scan < limit && data[scan] != '\n' && data[scan] != '*') ++scan;
                            if (scan > at) {
                                position_ += scan - at;
                                at = scan;
                                continuation_ = true;
                                if (at >= limit) break;
                            }
                        } else if (lexical_.mode == code_lexer::Mode::LineComment) {
                            size_t scan = at;
                            while (scan < limit && data[scan] != '\n' && data[scan] != '\\') ++scan;
                            if (scan > at) {
                                lexical_.escaped = false;
                                position_ += scan - at;
                                at = scan;
                                continuation_ = true;
                                if (at >= limit) break;
                            }
                        } else if (lexical_.mode == code_lexer::Mode::Code) {
                            size_t scan = at;
                            while (scan < limit && !is_code_trigger(static_cast<unsigned char>(data[scan]))) ++scan;
                            if (scan > at) {
                                position_ += scan - at;
                                at = scan;
                                continuation_ = true;
                                if (at >= limit) break;
                            }
                        }
                    }
                    const bool nl = data[at] == '\n';
                    code_lexer::advance_with_lookahead(lexical_, language_, std::string_view(data + at, std::min(total - at, code_lexer::lookaheadSize)));
                    ++position_;
                    ++at;
                    if (nl) {
                        column_ = 1;
                        continuation_ = false;
                        if (line_ == std::numeric_limits<int>::max()) {
                            error = "File exceeds supported line-number range";
                            finished_ = true;
                            break;
                        }
                        ++line_;
                    } else {
                        continuation_ = true;
                    }
                }
            }
        }

        while (!finished_ && at < total) {
            if (request_.action == ecs::FilePageRequest::Action::Previous && position_ >= request_.cursor.offset) {
                finished_ = true;
                break;
            }
            if (!eof && language_ != code_lexer::Language::Plain && total - at < code_lexer::lookaheadSize * 2) break;
            // Fast-collect contiguous ASCII runs for UTF-8 plain text (the common case for large files).
            if (enc_ == Enc::Utf8 && language_ == code_lexer::Language::Plain &&
                request_.action != ecs::FilePageRequest::Action::Previous &&
                !(request_.action == ecs::FilePageRequest::Action::Next && position_ < request_.cursor.offset) &&
                (!target || line_ >= firstLine) && position_ > 0 &&
                static_cast<unsigned char>(data[at]) <= 0x7f && data[at] != '\n') {
                const bool beforeTarget = target && (line_ < request_.targetLine || (line_ == request_.targetLine && column_ < request_.targetColumn));
                if (beforeTarget && raw.size() + 1 > byteLimit / 2) {
                    raw.clear();
                    pageLines_ = 0;
                }
                if (raw.size() + 1 > byteLimit || pageLines_ >= lineLimit) {
                    finished_ = true;
                    break;
                }
                const size_t cap = beforeTarget ? (byteLimit / 2 - raw.size()) : (byteLimit - raw.size());
                const size_t maxRun = std::min(total - at, std::min(cap, static_cast<size_t>(std::numeric_limits<int>::max() - column_)));
                size_t run = 0;
                while (run < maxRun && static_cast<unsigned char>(data[at + run]) <= 0x7f && data[at + run] != '\n') ++run;
                if (run > 0) {
                    reachedTarget_ |= line_ >= request_.targetLine;
                    if (raw.empty()) begin = {position_, line_, continuation_, column_, lexical_};
                    raw.append(data + at, run);
                    position_ += run;
                    at += run;
                    column_ += static_cast<int>(run);
                    continuation_ = true;
                    continue;
                }
            }

            auto size = unit_size(at, eof);
            if (size == 0) break;
            bool newline = enc_ == Enc::Utf16Le ? size == 2 && data[at] == '\n' && data[at + 1] == '\0' :
                enc_ == Enc::Utf16Be ? size == 2 && data[at] == '\0' && data[at + 1] == '\n' : size == 1 && data[at] == '\n';
            if (request_.action == ecs::FilePageRequest::Action::Next && position_ < request_.cursor.offset) {
                position_ += size;
                at += size;
                continue;
            }
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
                if (raw.empty()) begin = {position_, line_, continuation_, column_, lexical_};
                raw.append(data + at, size);
                if (newline) ++pageLines_;
            }
            const bool bom = position_ == 0 && (
                (enc_ == Enc::Utf8 && size == 3 && std::memcmp(data + at, "\xef\xbb\xbf", 3) == 0) ||
                (enc_ == Enc::Utf16Le && size == 2 && std::memcmp(data + at, "\xff\xfe", 2) == 0) ||
                (enc_ == Enc::Utf16Be && size == 2 && std::memcmp(data + at, "\xfe\xff", 2) == 0));
            if (!bom) advance_lexical(at);
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
        }
        if (at > 0) next = {position_, line_, continuation_, column_, lexical_};
        if (at == total) pending_.clear();
        else if (at > 0) pending_.erase(0, at);
        return !finished_;
    }
public:
    std::string raw;
    std::string encoding;
    std::string error;
    ecs::FilePageCursor begin;
    ecs::FilePageCursor next;

    Collector(ecs::FilePageRequest request, std::string overrideEncoding, const std::string& detected, uint64_t sourceOffset = 0, code_lexer::Language language = code_lexer::Language::Plain)
        : request_(request), position_(sourceOffset),
          line_(request.action == ecs::FilePageRequest::Action::Next ? request.cursor.line : 1),
          column_(request.action == ecs::FilePageRequest::Action::Next ? request.cursor.column : 1),
          continuation_(request.action == ecs::FilePageRequest::Action::Next && request.cursor.continuation),
          language_(language), lexical_(request.action == ecs::FilePageRequest::Action::Next ? request.cursor.lexical : code_lexer::State{}),
          encoding(overrideEncoding == "auto" ? detected : std::move(overrideEncoding)) {
        enc_ = classify_enc(encoding);
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
        if (position_ == 0 && request_.targetLine == 1 && request_.targetColumn == 1) reachedTarget_ = true;
        if (request_.action == ecs::FilePageRequest::Action::TargetLine && !reachedTarget_)
            error = "Requested line is beyond the end of this file";
        raw.shrink_to_fit();
    }
};

}
