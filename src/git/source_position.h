#pragma once

#include "../util/reading_workspace.h"
#include "../util/async_task.h"

namespace git {

struct SourcePosition {
    reading::SourceLocation location;
    std::string error;
};

SourcePosition locate_source_position(const std::string& repository, reading::SourceLocation location, std::stop_token stop = {}, const std::string& encoding = "auto");
async_work::Task<SourcePosition> locate_source_position_async(std::string repository, reading::SourceLocation location, std::string encoding = "auto");

}
