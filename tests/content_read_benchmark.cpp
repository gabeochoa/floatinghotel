#include "src/git/content_reader.h"
#include <chrono>
#include <cstdio>
#include <map>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::map<std::string, size_t> commands;
    git::set_log_callback([&](const std::string& command, const std::string&, const std::string&, bool) {
        for (const auto* name : {"rev-parse", "cat-file", "ls-tree", "ls-files"})
            if (command.find(name) != std::string::npos) ++commands[name];
    });
    for (int iteration = 0; iteration < 31; ++iteration) {
        commands.clear();
        const auto start = std::chrono::steady_clock::now();
        auto result = git::read_file({argv[1], argv[2], argv[3]});
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!result.error.empty()) { std::fprintf(stderr, "%s\n", result.error.c_str()); return 1; }
        std::printf("READ %d %.4f %zu %zu %zu %zu %zu\n", iteration, ms, result.raw.size(),
            commands["rev-parse"], commands["cat-file"], commands["ls-tree"], commands["ls-files"]);
    }
    git::set_log_callback({});
}
