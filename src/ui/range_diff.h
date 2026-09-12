#pragma once

#include <sstream>

#include "diff_renderer.h"
#include "../util/revision_range.h"

namespace ecs {

inline void start_range_diff(RepoComponent& repo) {
    auto& state = repo.rangeDiff;
    state.future = {};
    state.display.clear();
    state.error.clear();
    state.resolved = {};
    state.next = 0;
    auto oldRange = parse_revision_range(state.oldRange);
    auto newRange = parse_revision_range(state.newRange);
    if (!oldRange || !newRange) {
        state.error = "Enter two ranges with explicit endpoints, such as main..topic";
        return;
    }
    state.requestStamp = navigation::stamp(repo, state.oldRange + "\n" + state.newRange);
    state.revisions = {oldRange->first, oldRange->second, newRange->first, newRange->second};
    state.future = git::git_run_async(repo.repoPath, {"rev-parse", "--verify", "--end-of-options", state.revisions[0] + "^{commit}"});
}

inline void poll_range_diff(RepoComponent& repo) {
    auto& state = repo.rangeDiff;
    if (!repo.comparisonOpen() || !state.enabled) {
        state.future = {};
        return;
    }
    if (!state.future.valid() || state.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = state.future.get();
    if (!navigation::accepts(repo, state.requestStamp, state.oldRange + "\n" + state.newRange)) return;
    state.future = {};
    if (!result.success() && (!result.raw.outputStopped || result.raw.cancelled)) {
        state.error = result.stderr_str().empty() ? "Unable to compare commit series" : result.stderr_str();
        return;
    }
    if (state.next < state.resolved.size()) {
        auto hash = result.stdout_str();
        while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) hash.pop_back();
        state.resolved[state.next++] = std::move(hash);
        if (state.next < state.resolved.size())
            state.future = git::git_run_async(repo.repoPath, {"rev-parse", "--verify", "--end-of-options", state.revisions[state.next] + "^{commit}"});
        else state.future = async_work::launch([path = repo.repoPath, resolved = state.resolved](std::stop_token stop) {
            std::string output;
            constexpr size_t limit = 256 * 1024;
            auto result = git::git_run(path, {"range-diff", "--no-color", "--no-ext-diff", "--no-textconv",
                resolved[0] + ".." + resolved[1], resolved[2] + ".." + resolved[3]}, stop,
                [&](std::string_view chunk) {
                    output.append(chunk.substr(0, std::min(chunk.size(), limit - output.size())));
                    return output.size() < limit;
                });
            result.raw.stdout_str = std::move(output);
            return result;
        }, async_work::Priority::Foreground, git::GitResult{{"", "Background queue is full; retry comparing series", -1}});
        return;
    }
    FileDiff file;
    file.filePath = "Range diff";
    file.isFullContent = true;
    DiffHunk hunk;
    hunk.oldStart = hunk.newStart = 1;
    std::istringstream stream(result.stdout_str());
    std::string line;
    while (std::getline(stream, line)) hunk.lines.push_back(" " + line);
    if (!hunk.lines.empty() && !result.stdout_str().ends_with('\n')) hunk.noNewline.insert(hunk.lines.size() - 1);
    if (result.raw.outputStopped) {
        state.error = "Range diff limited to 256 KiB; narrow the ranges to see the remaining changes";
        hunk.lines.push_back(" [range-diff output truncated at 256 KiB]");
    }
    hunk.oldCount = hunk.newCount = static_cast<int>(hunk.lines.size());
    file.hunks.push_back(std::move(hunk));
    state.display = {std::move(file)};
}

inline void render_range_diff(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo, LayoutComponent& layout) {
    auto& state = repo.rangeDiff;
    auto field = [&](int id, const std::string& label, std::string& text, const std::string& name) {
        auto row = div(ctx, mk(parent, id), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
        div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(label)
            .with_size(ComponentSize{pixels(100), pixels(32)}).with_font_size(FontSize::Small));
        afterhours::text_input::text_input(ctx, mk(row.ent(), 1), text, ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(32)}).with_debug_name(name));
    };
    field(598001, "Old range", state.oldRange, "range_diff_old");
    field(598002, "New range", state.newRange, "range_diff_new");
    auto actions = div(ctx, mk(parent, 598003), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    if (button(ctx, mk(actions.ent(), 0), preset::Button("Compare series")
            .with_size(ComponentSize{pixels(145), pixels(30)}).with_debug_name("range_diff_submit"))) start_range_diff(repo);
    if (button(ctx, mk(actions.ent(), 1), preset::Button("Close")
            .with_size(ComponentSize{pixels(70), pixels(30)}).with_debug_name("range_diff_close"))) {
        navigation::open(repo, reading::review("wt"));
        state.future = {};
    }
    std::string status = "Compare rewritten, reordered, added, or dropped commits";
    if (state.future.valid()) status = state.next < 4 ? "Resolving range endpoints..." : "Comparing commit series...";
    else if (!state.error.empty()) status = state.error;
    else if (!state.display.empty()) status = "Resolved series: " + state.resolved[0].substr(0, 12) + ".." + state.resolved[1].substr(0, 12) +
        " vs " + state.resolved[2].substr(0, 12) + ".." + state.resolved[3].substr(0, 12);
    div(ctx, mk(parent, 598004), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("range_diff_status"));
    if (!state.display.empty())
        ui::render_diff(ctx, parent, state.display, layout.mainContent.width, layout.mainContent.height - 162.f,
            false, false, false, repo.repoPath, nullptr, "range-diff");
}

}
