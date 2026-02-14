#pragma once
#include <string>

namespace git {

// Convert raw git stderr to a user-friendly message.
// Returns the friendly message, or the raw stderr if no pattern matches.
std::string humanize_error(const std::string& stderr_str);

}  // namespace git
