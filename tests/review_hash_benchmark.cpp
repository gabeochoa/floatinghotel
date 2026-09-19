#include "src/ui/diff_metrics.h"
#include <chrono>
#include <cstdio>

int main() {
    ecs::FileDiff file;
    file.filePath = "large.cpp";
    file.hunks.push_back({1, 3000, 1, 3000, "@@ -1,3000 +1,3000 @@"});
    for (int i = 0; i < 3000; ++i) {
        file.hunks.back().lines.push_back("-int old_" + std::to_string(i) + " = 0;");
        file.hunks.back().lines.push_back("+int old_" + std::to_string(i) + " = 1;");
    }
    ui::DiffMetricsCache cache;
    const auto expected = ecs::ReviewComponent::hunk_key(file.filePath, file.hunks.front());
    for (bool cached : {false, true}) {
        const auto start = std::chrono::steady_clock::now();
        size_t bytes = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto value = cached ? cache.hunk_key(file, file.hunks.front()) :
                ecs::ReviewComponent::hunk_key(file.filePath, file.hunks.front());
            if (value != expected) return 1;
            bytes += value.size();
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s 1000 requests %.3f ms %zu output bytes %zu cached scans\n", cached ? "cached" : "uncached", ms, bytes, cache.hunk_scans());
    }
}
