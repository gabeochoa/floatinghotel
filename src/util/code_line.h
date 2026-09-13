#pragma once

#include <string_view>

namespace reading {

struct CodeLine {
    int number = 1;
    int column = 1;
    std::string_view text;
};

}
