#pragma once

// LSP base protocol framing: `Content-Length: N\r\n\r\n` then N bytes of JSON.
// The decoder takes bytes in whatever chunks a pipe delivers and yields whole
// messages; a malformed header or an oversized body is an error, not a hang.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace json_rpc {

inline std::string frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

class Decoder {
public:
    static constexpr size_t kMaxBody = 64 * 1024 * 1024;

    void feed(std::string_view bytes) { buffer_.append(bytes); }

    // The next complete body, if one has arrived. Sets error() on a header
    // that cannot be parsed; the stream is unusable after that.
    std::optional<std::string> next() {
        if (!error_.empty()) return std::nullopt;
        const size_t end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (buffer_.size() > 8192) error_ = "header too long";
            return std::nullopt;
        }
        std::optional<size_t> length;
        size_t start = 0;
        while (start < end) {
            size_t stop = buffer_.find("\r\n", start);
            if (stop == std::string::npos || stop > end) stop = end;
            std::string_view line(buffer_.data() + start, stop - start);
            constexpr std::string_view key = "Content-Length:";
            if (line.size() > key.size() && line.substr(0, key.size()) == key) {
                size_t value = 0;
                bool digits = false;
                for (char c : line.substr(key.size())) {
                    if (c == ' ') continue;
                    if (c < '0' || c > '9') { error_ = "bad Content-Length"; return std::nullopt; }
                    value = value * 10 + static_cast<size_t>(c - '0');
                    digits = true;
                    if (value > kMaxBody) { error_ = "message too large"; return std::nullopt; }
                }
                if (!digits) { error_ = "bad Content-Length"; return std::nullopt; }
                length = value;
            }
            start = stop + 2;
        }
        if (!length) { error_ = "missing Content-Length"; return std::nullopt; }
        if (buffer_.size() < end + 4 + *length) return std::nullopt;
        std::string body = buffer_.substr(end + 4, *length);
        buffer_.erase(0, end + 4 + *length);
        return body;
    }

    const std::string& error() const { return error_; }

private:
    std::string buffer_;
    std::string error_;
};

}  // namespace json_rpc
