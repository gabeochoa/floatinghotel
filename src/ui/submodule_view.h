#pragma once

#include <map>
#include "../ecs/ui_imports.h"
#include "../ecs/tab_bar_system.h"
#include "../git/reading_catalog.h"
#include "../util/async_task.h"

// Gitlink changes: the commit range the pointer moved across, read from the
// submodule checkout in the background, plus a route into that repository.
namespace ui::submodule_view {

struct Pointer {
    std::string before, after;
    bool dirty = false;
};

inline Pointer pointer(const ecs::FileDiff& file) {
    Pointer out;
    for (const auto& hunk : file.hunks)
        for (const auto& line : hunk.lines) {
            constexpr std::string_view prefix = "Subproject commit ";
            if (line.size() < 1 + prefix.size() || line.compare(1, prefix.size(), prefix) != 0) continue;
            auto id = line.substr(1 + prefix.size());
            if (id.ends_with("-dirty")) { id.resize(id.size() - 6); out.dirty = line.front() == '+'; }
            (line.front() == '-' ? out.before : out.after) = id;
        }
    return out;
}

struct Entry {
    unsigned generation = 0;
    git::catalog::SubmoduleRange range;
    async_work::Task<git::catalog::SubmoduleRange> task;
};
inline std::map<std::string, Entry>& cache() { static std::map<std::string, Entry> value; return value; }

inline bool pending() {
    for (auto& [key, entry] : cache())
        if (entry.task.valid() && entry.task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    return false;
}

// Re-reads when the repository refreshes; shows the last answer meanwhile.
inline const git::catalog::SubmoduleRange& range(const std::string& directory, const Pointer& pointer, unsigned generation) {
    if (cache().size() >= 64) cache().clear();
    auto& entry = cache()[directory + "\n" + pointer.before + "\n" + pointer.after];
    if (entry.task.valid() && entry.task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { entry.range = entry.task.get(); }
        catch (const std::exception& error) { entry.range = {error.what()}; }
    }
    if (!entry.task.valid() && (entry.generation != generation || entry.range.summary.empty())) {
        entry.generation = generation;
        if (entry.range.summary.empty()) entry.range.summary = "Reading submodule commits...";
        entry.task = async_work::launch([directory, pointer](std::stop_token stop) {
            return git::catalog::submodule_range(directory, pointer.before, pointer.after, stop);
        }, async_work::Priority::Foreground, git::catalog::SubmoduleRange{"Background queue is full; refresh to retry"});
    }
    return entry.range;
}

inline float height(const ecs::FileDiff& file, const std::string& repoPath, unsigned generation) {
    const auto& value = range((std::filesystem::path(repoPath) / file.filePath).string(), pointer(file), generation);
    return 36.f + 18.f * static_cast<float>(value.commits.size()) + (value.checkedOut ? 28.f : 0.f);
}

inline void render(UIContext<InputAction>& ctx, Entity& parent, int id, const ecs::FileDiff& file,
                   const std::string& repoPath, unsigned generation, afterhours::ui::Size width) {
    const auto ids = pointer(file);
    const auto directory = (std::filesystem::path(repoPath) / file.filePath).string();
    const auto& value = range(directory, ids, generation);
    auto block = div(ctx, mk(parent, id), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{width, children()}).with_flex_direction(FlexDirection::Column)
        .with_padding(Padding{.top = pixels(6), .right = pixels(12), .bottom = pixels(6), .left = pixels(12)})
        .with_custom_background(theme::PANEL_BG).with_debug_name("submodule_summary"));
    const auto shortId = [](const std::string& value) { return value.empty() ? std::string("none") : value.substr(0, 12); };
    std::string text = "Submodule " + shortId(ids.before) + " → " + shortId(ids.after) +
        (ids.dirty ? " · checkout has local changes" : "") + "\n" + value.summary;
    for (const auto& commit : value.commits) text += "\n" + commit;
    div(ctx, mk(block.ent(), 1), ComponentConfig{}.with_skip_grid_snap().with_label(text)
        .with_size(ComponentSize{percent(1.f), pixels(24.f + 18.f * static_cast<float>(value.commits.size()))})
        .with_font("mono", pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
        .with_alignment(TextAlignment::Left).with_custom_text_color(theme::TEXT_SECONDARY)
        .with_debug_name("submodule_range"));
    if (value.checkedOut && button(ctx, mk(block.ent(), 2), preset::Button("Open submodule")
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
            .with_debug_name("open_submodule"))) {
        if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) ecs::TabBarSystem::open_repository(directory, *layout);
    }
}

}
