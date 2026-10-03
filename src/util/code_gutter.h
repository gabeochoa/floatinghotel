#pragma once

#include <string>

namespace code_gutter {

// View > Line Numbers. Off drops the number columns; the +/- sign stays.
inline bool showNumbers = true;

inline std::string pad(const std::string& number, size_t width = 5) {
    if (!showNumbers) return "";
    return std::string(number.size() < width ? width - number.size() : 0, ' ') + number;
}

inline std::string prefix(const std::string& oldNumber, const std::string& newNumber,
                          char sign, bool fullContent) {
    if (fullContent) return "  " + pad(newNumber) + "  ";
    return pad(oldNumber) + " " + pad(newNumber) + "  " + sign + " ";
}

}
