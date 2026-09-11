#pragma once

#include "git/git_runner.h"

namespace review_store {

std::future<git::GitResult> snapshot_async(std::string repo, std::string path,
    std::string head, bool capture, int context, bool ignoreWhitespace);

}
