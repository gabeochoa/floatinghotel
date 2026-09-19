#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>
#include <string>
#include <vector>

namespace git {

inline std::string redact_log_text(std::string text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<std::pair<size_t, size_t>> spans;
    for (size_t pos = 0; (pos = lower.find("://", pos)) != std::string::npos; pos += 3) {
        const auto at = lower.find('@', pos + 3);
        const auto end = lower.find_first_of("/ \t\r\n", pos + 3);
        if (at != std::string::npos && (end == std::string::npos || at < end)) spans.emplace_back(pos + 3, at);
    }
    for (std::string_view key : {"authorization", "password", "passwd", "access_token", "access-token", "oauth_token", "oauth-token", "token", "api_key", "api-key", "apikey", "client_secret", "client-secret"}) {
        for (size_t pos = 0; (pos = lower.find(key, pos)) != std::string::npos; pos += key.size()) {
            if (pos && std::isalnum(static_cast<unsigned char>(lower[pos - 1]))) continue;
            auto value = lower.find_first_not_of(" \t", pos + key.size());
            if (value == std::string::npos || (lower[value] != ':' && lower[value] != '=')) continue;
            value = lower.find_first_not_of(" \t", value + 1);
            if (value == std::string::npos) continue;
            if (lower[value] == '\'' || lower[value] == '"') ++value;
            const auto end = lower.find_first_of(key == "authorization" ? "\r\n" : " \t\r\n&;\"'", value);
            spans.emplace_back(value, end == std::string::npos ? lower.size() : end);
        }
    }
    std::sort(spans.begin(), spans.end());
    std::string safe;
    size_t cursor = 0;
    for (size_t i = 0; i < spans.size(); ++i) {
        auto [first, last] = spans[i];
        while (i + 1 < spans.size() && spans[i + 1].first <= last) last = std::max(last, spans[++i].second);
        if (last <= first) continue;
        safe.append(text, cursor, first - cursor);
        safe += "<redacted>";
        cursor = last;
    }
    safe.append(text, cursor, text.size() - cursor);
    return safe;
}

inline std::string bounded_log_output(std::string_view text) {
    constexpr size_t limit = 65536;
    if (text.size() <= limit) return std::string(text);
    const auto end = text.rfind('\n', limit);
    return (end == text.npos ? "" : std::string(text.substr(0, end + 1))) + "[command log output limit reached]";
}

struct RedactedLog {
    std::string command;
    std::string output;
    std::string error;
};

inline RedactedLog redact_git_log(const std::vector<std::string>& arguments,
                                 std::string output, std::string error) {
    std::vector<std::string> secrets;
    std::vector<std::string> safeArguments;
    bool secretNext = false;
    for (const auto& argument : arguments) {
        auto lower = argument;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (secretNext) {
            if (!argument.empty()) secrets.push_back(argument);
            safeArguments.emplace_back("<redacted>");
        } else safeArguments.push_back(redact_log_text(argument));
        secretNext = lower == "--password" || lower == "--passwd" || lower == "--token" ||
            lower == "--oauth-token" || lower == "--access-token" || lower == "--api-key" ||
            lower == "--client-secret";
        auto scheme = argument.find("://");
        if (scheme != std::string::npos) {
            auto end = argument.find_first_of("/ \t\r\n", scheme + 3);
            auto at = argument.find('@', scheme + 3);
            if (at != std::string::npos && (end == std::string::npos || at < end)) {
                auto userinfo = argument.substr(scheme + 3, at - scheme - 3);
                secrets.push_back(userinfo);
                auto colon = userinfo.find(':');
                if (colon != std::string::npos && colon + 1 < userinfo.size())
                    secrets.push_back(userinfo.substr(colon + 1));
            }
        }
        for (const auto& key : {"password=", "passwd=", "token=", "api_key=", "api-key=", "client_secret="}) {
            auto at = lower.find(key);
            if (at == std::string::npos) continue;
            auto begin = at + std::string(key).size();
            auto end = argument.find_first_of("&; \t\r\n\"'", begin);
            if (begin < argument.size() && end != begin) secrets.push_back(argument.substr(begin, end - begin));
        }
        auto auth = lower.find("authorization:");
        if (auth == std::string::npos) auth = lower.find("authorization=");
        if (auth != std::string::npos) {
            auto begin = argument.find_first_not_of(" \t", auth + 14);
            if (begin != std::string::npos) {
                auto token = argument.find_first_of(" \t", begin);
                if (token != std::string::npos) {
                    token = argument.find_first_not_of(" \t", token);
                    if (token != std::string::npos) secrets.push_back(argument.substr(token));
                }
            }
        }
    }
    auto sanitize = [&](std::string value) {
        std::sort(secrets.begin(), secrets.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
        for (const auto& secret : secrets) {
            size_t at = 0;
            while (!secret.empty() && (at = value.find(secret, at)) != std::string::npos) {
                value.replace(at, secret.size(), "<redacted>");
                at += 10;
            }
        }
        return redact_log_text(std::move(value));
    };
    std::string command;
    for (const auto& argument : safeArguments) {
        if (!command.empty()) command += ' ';
        command += argument;
    }
    return {sanitize(std::move(command)), sanitize(std::move(output)), sanitize(std::move(error))};
}

}
