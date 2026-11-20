#pragma once

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace ecs { struct BlameLine; }

namespace code_gutter {

// View > Line Numbers. Off drops the number columns; the +/- sign stays.
inline bool showNumbers = true;

// Blame gutter: returns the blame text for a 1-based line, already padded
// to blameChars. Lent by the full-file view and by single-file diffs for
// the duration of one render; null everywhere else.
inline std::function<std::string(int)> blameFor;
inline int blameChars = 0;

// Format parsed blame lines as the gutter text and lend it to prefix().
// Defined in diff_renderer.h, where ecs::BlameLine is complete; declared
// here so prefix() and the unit test TU need no component types.
inline void lend_blame(const std::vector<ecs::BlameLine>& lines);

inline void clear_blame() { blameFor = nullptr; blameChars = 0; }

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
    // Diff lines: blame (when lent by the diff renderer for a single-file
    // diff) sits between the line numbers and the sign, keyed by the
    // new-side number.
    if (blameFor && !newNumber.empty())
        return pad(oldNumber) + " " + pad(newNumber) + "  " + blameFor(std::atoi(newNumber.c_str())) + " " + sign + " ";
    return pad(oldNumber) + " " + pad(newNumber) + "  " + sign + " ";
}

}
