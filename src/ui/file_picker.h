#pragma once

#include "../ecs/ui_imports.h"
#include "../util/fuzzy_match.h"
#include "focus.h"
#include "virtual_list.h"
#include <afterhours/src/plugins/modal.h>

namespace ecs {

inline std::string file_picker_revision_label(const reading::SourceRevision& revision) {
    if (std::holds_alternative<reading::WorkingTree>(revision)) return "Working tree";
    if (std::holds_alternative<reading::Index>(revision)) return "Staged";
    const auto& name = reading::revision_text(revision);
    return std::holds_alternative<reading::ObjectId>(revision) ? "Commit " + name.substr(0, 7) : name;
}

inline void update_file_picker_paths(RepoComponent& repo, LayoutComponent& layout) {
    auto& scope = layout.filePickerScope;
    const reading::SourceRevision revision = scope.workingTree ? reading::SourceRevision{reading::WorkingTree{}} : scope.documentRevision;
    std::string key = "paths:" + reading::revision_text(revision) + ":" + std::to_string(repo.dataGeneration);
    if (std::holds_alternative<reading::WorkingTree>(revision) || std::holds_alternative<reading::Index>(revision))
        key += ":" + std::to_string(repo.repoVersion) + ":" + std::to_string(repo.allFilePathsGeneration);
    if (scope.request.key != key || !navigation::accepts(repo, scope.request, key)) {
        scope.future = {};
        scope.listing = git::PathList{revision};
        scope.request = navigation::stamp(repo, key);
        layout.filePickerCacheKey.clear();
        if (!std::holds_alternative<reading::WorkingTree>(revision))
            scope.future = git::load_paths_async(repo.repoPath, revision);
    }
    if (scope.future.valid() && scope.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = scope.future.get();
        if (layout.filePickerOpen && navigation::accepts(repo, scope.request, key)) {
            scope.listing = std::move(result);
            layout.filePickerCacheKey.clear();
        }
    }
}

inline std::vector<afterhours::ui::TextSpan> file_picker_label(const std::string& path, const std::string& query, bool selected) {
    const auto ranges = fuzzy::matched_ranges(query, path);
    const auto filename = fuzzy::filename_start(path);
    std::vector<afterhours::ui::TextSpan> spans;
    const auto directoryColor = selected ? afterhours::Color{196, 202, 212, 255} : theme::TEXT_SECONDARY;
    auto append = [&](size_t begin, size_t finish) {
        size_t match = 0;
        int previousStyle = -1;
        for (size_t at = begin; at < finish;) {
            const auto end = code_wrap::next_codepoint(path, at);
            while (match < ranges.size() && ranges[match].second <= at) ++match;
            const bool highlighted = match < ranges.size() && ranges[match].first <= at;
            const auto color = highlighted ? afterhours::Color{190, 215, 255, 255} :
                at < filename ? directoryColor : theme::TEXT_PRIMARY;
            const int style = highlighted ? 2 : at < filename ? 0 : 1;
            if (previousStyle == style) spans.back().text += path.substr(at, end - at);
            else spans.push_back({path.substr(at, end - at), color});
            previousStyle = style;
            at = end;
        }
    };
    append(filename, path.size());
    if (filename > 0) {
        spans.push_back({"  ", directoryColor});
        append(0, filename - 1);
    }
    return spans;
}

inline void render_file_picker(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout, float height) {
    bool revealSelection = layout.filePickerFocus;
    ui::bind_focus(parent, repo, reading::focus::Region::Picker);
    auto& scope = layout.filePickerScope;
    auto heading = div(ctx, mk(parent, 586000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row).with_gap(pixels(6)));
    div(ctx, mk(heading.ent(), 0), ComponentConfig{}.with_label("Go to file")
        .with_size(ComponentSize{children(), pixels(30)}).with_font_size(pixels(14)));
    if (button(ctx, mk(heading.ent(), 1), preset::Button(file_picker_revision_label(scope.documentRevision))
            .with_size(ComponentSize{children(), pixels(28)})
            .with_custom_background(!scope.workingTree ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("file_picker_document_scope"))) {
        scope.workingTree = false;
        layout.filePickerFocus = true;
    }
    if (!std::holds_alternative<reading::WorkingTree>(scope.documentRevision) &&
        button(ctx, mk(heading.ent(), 2), preset::Button("Working tree")
            .with_size(ComponentSize{children(), pixels(28)})
            .with_custom_background(scope.workingTree ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("file_picker_working_scope"))) {
        scope.workingTree = true;
        layout.filePickerFocus = true;
    }
    if (!scope.listing.error.empty() && button(ctx, mk(heading.ent(), 3), preset::Button("Retry")
            .with_size(ComponentSize{children(), pixels(28)}).with_debug_name("file_picker_retry"))) {
        scope.request.key.clear();
        layout.filePickerFocus = true;
    }
    update_file_picker_paths(repo, layout);
    const bool working = std::holds_alternative<reading::WorkingTree>(scope.listing.revision);
    const auto& paths = working ? repo.allFilePaths : scope.listing.paths;
    auto input = afterhours::text_input::text_input(ctx, mk(parent, 586001), layout.filePickerQuery,
        ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("file_picker_input"));
    if (layout.filePickerFocus || ui::shortcut_owner(ctx, repo).region != reading::focus::Region::Picker) {
        ui::focus_control(ctx, input.ent());
        layout.filePickerFocus = false;
    }
    const auto selectionKey = repo.repoPath + "\n" + reading::revision_text(scope.listing.revision) + "\n" + layout.filePickerQuery;
    if (layout.filePickerSelectionKey != selectionKey) {
        layout.filePickerSelectedPath.clear();
        layout.filePickerSelectionKey = selectionKey;
    }
    std::string key = repo.repoPath + ":" + std::to_string(repo.dataGeneration) + ":" +
                      std::to_string(repo.repoVersion) + "\n" + scope.request.key + "\n" + layout.filePickerQuery;
    if (key != layout.filePickerCacheKey) {
        layout.filePickerResults = fuzzy::rank(paths, layout.filePickerQuery, repo.workspace().recent_source_paths(scope.listing.revision));
        layout.filePickerCacheKey = key;
        layout.filePickerIndex = 0;
        const auto selected = std::find(layout.filePickerResults.begin(), layout.filePickerResults.end(), layout.filePickerSelectedPath);
        if (selected != layout.filePickerResults.end()) layout.filePickerIndex = static_cast<int>(selected - layout.filePickerResults.begin());
        revealSelection = true;
    }
    auto open = [&](const std::string& path, bool keep) {
        navigation::click(repo, reading::SourceLocation{{path, scope.listing.revision}}, keep, reading::ClickRegion::Picker);
    };
    auto& results = layout.filePickerResults;
    const bool pickerKeys = !ui::shortcuts_blocked(layout) && ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Picker);
    if (pickerKeys && !results.empty()) {
        if (afterhours::input::is_key_pressed(264)) layout.filePickerIndex = std::min(layout.filePickerIndex + 1, static_cast<int>(results.size()) - 1);
        if (afterhours::input::is_key_pressed(265)) layout.filePickerIndex = std::max(0, layout.filePickerIndex - 1);
        if (afterhours::input::is_key_pressed(257)) open(results[layout.filePickerIndex], true);
    }
    if (!results.empty()) layout.filePickerSelectedPath = results[layout.filePickerIndex];
    const auto& error = working ? repo.filesError : scope.listing.error;
    const std::string status = scope.future.valid() ? "Loading files..." : !error.empty() ? error :
        std::to_string(results.size()) + (scope.listing.truncated ? " matches · file list limit reached" : (layout.filePickerQuery.empty() ? " files · recent first · Enter open · Esc close" : " matches · arrows to choose · Enter open · Esc close"));
    div(ctx, mk(parent, 586002), ComponentConfig{}
        .with_label(status).with_debug_name("file_picker_status")
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
    const auto listParent = mk(parent, 586003);
    const bool moving = pickerKeys && (afterhours::input::is_key_pressed(264) || afterhours::input::is_key_pressed(265));
    if (revealSelection || moving) {
        auto [entity, owner] = afterhours::ui::imm::deref(listParent);
        if (entity.has<afterhours::ui::HasScrollView>()) {
            auto& scroll = entity.get<afterhours::ui::HasScrollView>();
            const float rowHeight = 28.f * ui::zoom::get();
            const float top = static_cast<float>(layout.filePickerIndex) * rowHeight;
            const float viewport = std::max(28.f, height - 90.f) * ui::zoom::get();
            float target = scroll.scroll_offset.y;
            if (top < target) target = top;
            else if (top + rowHeight > target + viewport) target = top + rowHeight - viewport;
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = std::max(0.f, target);
            scroll.anchor_child = -1;
        }
        if (moving) ui::focus_control(ctx, input.ent());
    }
    ui::virtual_list(ctx, listParent, results.size(), 28.f,
        [&](size_t i, Entity& row) {
            auto result = button(ctx, mk(row, 0), preset::Button(results[i])
                    .with_styled_label(file_picker_label(results[i], layout.filePickerQuery, static_cast<int>(i) == layout.filePickerIndex))
                    .with_tooltip(results[i])
                    .with_size(ComponentSize{percent(1.f), pixels(28)})
                    .with_alignment(TextAlignment::Left)
                    .with_custom_background(static_cast<int>(i) == layout.filePickerIndex ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                    .with_font_size(pixels(14)).with_debug_name("file_picker_result"));
            ui::bind_focus(result.ent(), repo, reading::focus::Region::Picker, results[i]);
            if (result) open(results[i], false);
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(28.f, height - 90.f))})
            .with_debug_name("file_picker_list"));
}

struct FilePickerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!layout) return;
        if (!repo || repo->repoPath.empty()) layout->filePickerOpen = false;
        if (layout->filePickerOpen && repo) {
            auto& scope = layout->filePickerScope;
            if (!scope.owner || scope.owner->repository != repo->repoPath ||
                    scope.owner->generation != repo->workspace().generation() ||
                    !reading::same_document(scope.owner->document, repo->workspace().location())) {
                scope = {};
                scope.owner = navigation::stamp(*repo, {});
                scope.documentRevision = reading::source_revision_for(repo->workspace().location());
                layout->filePickerCacheKey.clear();
            }
        }
        auto& root = ui_imm::getUIRootEntity();
        const float zoom = ui::zoom::get();
        const float screenWidth = ctx.screen_width / zoom;
        const float screenHeight = ctx.screen_height / zoom;
        const float width = std::max(100.f, std::min(620.f, screenWidth - 32.f));
        const float height = std::max(160.f, std::min(440.f, screenHeight - 80.f));
        auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 586100), layout->filePickerOpen,
            afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
                .with_show_close_button(false).with_closed_by(afterhours::ClosedBy::Any)
                .with_backdrop_color({0, 0, 0, 0}));
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element = -1;
        if (!modal || !repo) {
            if (layout->filePickerScope.owner) {
                layout->filePickerScope = {};
                layout->filePickerResults.clear();
                layout->filePickerCacheKey.clear();
            }
            ctx.remove_input_gate("file_picker");
            return;
        }
        modal.cmp().absolute_pos_x = (ctx.screen_width - width * zoom) * .5f;
        modal.cmp().absolute_pos_y = (ctx.screen_height - height * zoom) * .3f;
        div(ctx, mk(root, 586101), ComponentConfig{}
            .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
            .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 64})
            .with_roundness(0.f).with_render_layer(998).with_debug_name("file_picker_backdrop"));
        auto body = div(ctx, mk(modal.ent(), 586102), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap()
            .with_render_layer(1001).with_debug_name("file_picker_overlay"));
        const auto modalId = modal.ent().id;
        ctx.add_input_gate("file_picker", [modalId](afterhours::EntityID id) {
            return afterhours::modal::detail::is_entity_in_tree(modalId, id);
        });
        render_file_picker(ctx, body.ent(), *repo, *layout, height - 48.f);
    }
};

}
