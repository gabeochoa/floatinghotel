#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace grep_capture {

inline constexpr size_t byteLimit = 4 * 1024 * 1024;
inline constexpr size_t matchLimit = 5000;

class Collector {
    enum class Field { Path, Line, Text };
    Field field_ = Field::Path;
    std::string output_;
    size_t complete_ = 0;
    size_t bytes_ = 0;
    size_t matches_ = 0;
    size_t byteLimit_;
    size_t matchLimit_;
    bool truncated_ = false;

    bool stop() {
        truncated_ = true;
        output_.resize(complete_);
        return false;
    }
public:
    explicit Collector(size_t bytes = byteLimit, size_t matches = matchLimit)
        : byteLimit_(bytes), matchLimit_(matches) {}

    bool consume(std::string_view chunk) {
        if (truncated_) return false;
        for (char c : chunk) {
            if (bytes_ == byteLimit_ || matches_ == matchLimit_) return stop();
            if (output_.size() == output_.capacity())
                output_.reserve(std::min(byteLimit_, output_.capacity() * 2));
            output_.push_back(c);
            ++bytes_;
            switch (field_) {
                case Field::Path:
                    if (c == '\0') field_ = Field::Line;
                    break;
                case Field::Line:
                    if (c == '\0') field_ = Field::Text;
                    break;
                case Field::Text:
                    if (c == '\n') {
                        field_ = Field::Path;
                        complete_ = output_.size();
                        ++matches_;
                    }
                    break;
            }
            if (bytes_ == byteLimit_ || matches_ == matchLimit_) return stop();
        }
        return true;
    }

    void finish() {
        if (output_.size() != complete_) stop();
    }
    const std::string& output() const { return output_; }
    size_t bytes() const { return bytes_; }
    size_t matches() const { return matches_; }
    bool truncated() const { return truncated_; }
};

}
