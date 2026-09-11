#pragma once

#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace git {

struct HistoryQuery {
    std::string message;
    std::string author;
    std::string since;
    std::string until;
    std::string path;
};

inline bool valid_history_date(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    int year = 0, month = 0, day = 0;
    auto parse = [&](size_t from, size_t to, int& result) {
        auto [end, error] = std::from_chars(value.data() + from, value.data() + to, result);
        return error == std::errc{} && end == value.data() + to;
    };
    return parse(0, 4, year) && parse(5, 7, month) && parse(8, 10, day) &&
        std::chrono::year_month_day{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}}.ok();
}

inline std::optional<std::vector<std::string>> history_search_args(const HistoryQuery& query, int limit) {
    if (!valid_history_date(query.since) || !valid_history_date(query.until) ||
        (!query.since.empty() && !query.until.empty() && query.since > query.until)) return std::nullopt;
    std::vector<std::string> args{"log", "--all", "--fixed-strings", "--regexp-ignore-case", "-n", std::to_string(limit),
        "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P"};
    if (!query.message.empty()) args.push_back("--grep=" + query.message);
    if (!query.author.empty()) args.push_back("--author=" + query.author);
    if (!query.since.empty()) args.push_back("--since=" + query.since + "T00:00:00");
    if (!query.until.empty()) args.push_back("--until=" + query.until + "T23:59:59");
    args.push_back("--");
    if (!query.path.empty()) args.push_back(":(literal)" + query.path);
    return args;
}

}
