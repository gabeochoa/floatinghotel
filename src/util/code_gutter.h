#pragma once

#include <cstdlib>
#include <functional>
#include <string>

namespace code_gutter {

// View > Line Numbers. Off drops the number columns; the +/- sign stays.
inline bool showNumbers = true;

// Blame gutter (full-file view only): returns the blame text for a 1-based
// line, already padded to blameChars. Set by the full-file view for the
// duration of one render; null everywhere else.
inline std::function<std::string(int)> blameFor;
inline int blameChars = 0;

inline std::string pad(const std::string& number, size_t width = 5) {
    if (!showNumbers) return "";
    return std::string(number.size() < width ? width - number.size() : 0, ' ') + number;
}

inline std::string prefix(const std::string& oldNumber, const std::string& newNumber,
                          char sign, bool fullContent) {
    if (fullContent) {
        std::string out = "  " + pad(newNumber) + "  ";
        if (blameFor && !newNumber.empty()) out += blameFor(std::atoi(newNumber.c_str())) + " ";
        return out;
    }
    return pad(oldNumber) + " " + pad(newNumber) + "  " + sign + " ";
}

}
