#pragma once

#include "../ecs/ui_imports.h"
#include "../util/fuzzy_match.h"

namespace ecs {

inline void render_file_picker(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout) {
    div(ctx, mk(parent, 586000), ComponentConfig{}
        .with_label("Go to file · working tree")
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Medium));
    auto input = afterhours::text_input::text_input(ctx, mk(parent, 586001), layout.filePickerQuery,
        ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("file_picker_input"));
    if (layout.filePickerFocus) { ctx.set_focus(input.ent().id); layout.filePickerFocus = false; }
    std::string key = repo.repoPath + ":" + std::to_string(repo.dataGeneration) + ":" +
                      std::to_string(repo.repoVersion) + "\n" + layout.filePickerQuery;
    if (key != layout.filePickerCacheKey) {
        layout.filePickerResults = fuzzy::rank(repo.allFilePaths, layout.filePickerQuery);
        layout.filePickerCacheKey = key;
        layout.filePickerIndex = 0;
    }
    auto open = [&](const std::string& path) {
        repo.selectedFilePath = path;
        repo.selectedFileStaged = false;
        repo.selectedCommitHash.clear();
        repo.fullFilePath = path;
        repo.activeContent = RepoComponent::ContentView::Source;
        repo.fullFileRevision.clear();
        repo.fullFileCacheKey.clear();
        repo.fullFileTargetLine = 0;
        layout.filePickerOpen = false;
        ctx.set_focus(ctx.ROOT);
    };
    auto& results = layout.filePickerResults;
    if (!results.empty()) {
        if (afterhours::input::is_key_pressed(264)) layout.filePickerIndex = std::min(layout.filePickerIndex + 1, static_cast<int>(results.size()) - 1);
        if (afterhours::input::is_key_pressed(265)) layout.filePickerIndex = std::max(0, layout.filePickerIndex - 1);
        if (afterhours::input::is_key_pressed(257)) open(results[layout.filePickerIndex]);
    }
    div(ctx, mk(parent, 586002), ComponentConfig{}
        .with_label(repo.filesError.empty() ? std::to_string(results.size()) + " matches · arrows to choose, Enter to open, Escape to close" : repo.filesError)
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(FontSize::Small));
    auto list = afterhours::ui::imm::virtual_list(ctx, mk(parent, 586003), results.size(), 28.f,
        [&](size_t i, Entity& row) {
            if (button(ctx, mk(row, 0), preset::Button(results[i])
                    .with_size(ComponentSize{percent(1.f), pixels(28)})
                    .with_alignment(TextAlignment::Left)
                    .with_custom_background(static_cast<int>(i) == layout.filePickerIndex ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                    .with_font_size(FontSize::Small).with_debug_name("file_picker_result"))) open(results[i]);
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(40.f, layout.mainContent.height - 90.f))})
            .with_debug_name("file_picker_list"));
    if (list.ent().has<afterhours::ui::HasScrollView>() &&
        (afterhours::input::is_key_pressed(264) || afterhours::input::is_key_pressed(265))) {
        auto& scroll = list.ent().get<afterhours::ui::HasScrollView>();
        float target = std::clamp(layout.filePickerIndex * 28.f - scroll.viewport_or_zero().y * 0.5f,
                                   0.f, std::max(0.f, scroll.content_size.y - scroll.viewport_or_zero().y));
        scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = target;
        ctx.set_focus(input.ent().id);
    }
}

}
