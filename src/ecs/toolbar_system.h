#pragma once

#include <string>

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

namespace ecs {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;

// ToolbarSystem: Renders action buttons.
// When sidebar is visible, renders as a compact strip inside the sidebar column.
// When sidebar is hidden, renders as a full-width toolbar below the menu bar.
struct ToolbarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<RepoComponent>()
                                .gen();

        // Get repo state for enable/disable logic
        bool hasRepo = !repoEntities.empty();
        bool hasUnstaged = false;
        bool hasStaged = false;
        std::string branchName = "main";

        if (hasRepo) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            hasUnstaged = !repo.unstagedFiles.empty() || !repo.untrackedFiles.empty();
            hasStaged = !repo.stagedFiles.empty();
            if (!repo.currentBranch.empty()) {
                branchName = repo.currentBranch;
            }
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float toolbarX = layout.toolbar.x;
        float w = layout.toolbar.width;
        float h = layout.toolbar.height;
        float toolbarY = layout.toolbar.y;

        bool inSidebar = layout.sidebarVisible;

        // Outer chrome container (absolute positioned)
        auto topChrome = div(ctx, mk(uiRoot, 5000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(toolbarX, toolbarY)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("top_chrome"));

        if (inSidebar) {
            render_sidebar_toolbar(ctx, topChrome, w, h,
                                   repoEntities, hasRepo, hasUnstaged, hasStaged);
        } else {
            render_fullwidth_toolbar(ctx, topChrome, w, h,
                                     repoEntities, hasRepo, hasUnstaged, hasStaged,
                                     branchName);
        }
    }

private:
    // Compact toolbar for sidebar: two rows of small buttons
    template<typename Result, typename Entities>
    void render_sidebar_toolbar(UIContext<InputAction>& ctx,
                                Result& topChrome,
                                float w, float /*h*/,
                                Entities& repoEntities,
                                bool hasRepo, bool hasUnstaged, bool hasStaged) {
        // Background matches sidebar
        auto toolbarBg = div(ctx, mk(topChrome.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), children()})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(6),
                    .bottom = h720(4), .left = w1280(6)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_sidebar"));

        // Bottom border
        div(ctx, mk(topChrome.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_border"));

        int nextId = 1100;

        // Row 1: Refresh | Stage | Unstage
        auto row1 = div(ctx, mk(toolbarBg.ent(), 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_row1"));

        auto sidebarBtn = [&](Entity& parent, int id,
                              const std::string& label, bool enabled) -> bool {
            auto btnBg = enabled ? afterhours::Color{65, 65, 70, 255}
                                 : afterhours::Color{50, 50, 53, 255};
            auto textCol = enabled ? afterhours::Color{220, 220, 220, 255}
                                   : afterhours::Color{100, 100, 100, 255};
            float btnW = static_cast<float>(label.size()) * 7.5f + 12.0f;
            auto config = ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{w1280(btnW), h720(20)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(5),
                    .bottom = h720(2), .left = w1280(5)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(3)})
                .with_custom_background(btnBg)
                .with_custom_text_color(textCol)
                .with_font_size(h720(theme::layout::FONT_META))
                .with_roundness(0.08f)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("stoolbar_btn");
            config.disabled = !enabled;
            return static_cast<bool>(button(ctx, mk(parent, id), config));
        };

        // Row 1 buttons
        if (sidebarBtn(row1.ent(), nextId++, "Refresh", hasRepo)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }
        if (sidebarBtn(row1.ent(), nextId++, "Stage", hasRepo && hasUnstaged)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }
        if (sidebarBtn(row1.ent(), nextId++, "Unstage", hasRepo && hasStaged)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }

        // Row 2: Commit | Push | Pull | Fetch
        auto row2 = div(ctx, mk(toolbarBg.ent(), 20),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{.top = h720(2)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_row2"));

        if (sidebarBtn(row2.ent(), nextId++, "Commit", hasRepo && hasStaged)) {
            auto editorEntities = afterhours::EntityQuery({.force_merge = true})
                                      .whereHasComponent<CommitEditorComponent>()
                                      .gen();
            if (!editorEntities.empty()) {
                editorEntities[0].get().get<CommitEditorComponent>()
                    .commitRequested = true;
            }
        }
        if (sidebarBtn(row2.ent(), nextId++, "Push", hasRepo)) {
            // Async push handled by T046
        }
        if (sidebarBtn(row2.ent(), nextId++, "Pull", hasRepo)) {
            // Async pull handled by T046
        }
        if (sidebarBtn(row2.ent(), nextId++, "Fetch", hasRepo)) {
            // Async fetch handled by T046
        }
    }

    // Full-width toolbar (when no sidebar)
    template<typename Result, typename Entities>
    void render_fullwidth_toolbar(UIContext<InputAction>& ctx,
                                  Result& topChrome,
                                  float w, float h,
                                  Entities& repoEntities,
                                  bool hasRepo, bool hasUnstaged, bool hasStaged,
                                  const std::string& branchName) {
        // Top border
        div(ctx, mk(topChrome.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_top_border"));

        // Toolbar row
        auto toolbarBg = div(ctx, mk(topChrome.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h - 2)})
                .with_custom_background(theme::TOOLBAR_BG)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(theme::layout::SMALL_PADDING),
                    .right = w1280(theme::layout::PADDING),
                    .bottom = h720(theme::layout::SMALL_PADDING),
                    .left = w1280(theme::layout::PADDING)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar"));

        // Bottom border
        div(ctx, mk(topChrome.ent(), 4),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_border"));

        int nextId = 1110;
        auto toolbarButton = [&](const std::string& label, bool enabled) -> bool {
            int id = nextId++;
            auto btnBg = enabled ? afterhours::Color{62, 62, 66, 255}
                                 : afterhours::Color{55, 55, 58, 0};
            auto textCol = enabled ? afterhours::Color{200, 200, 200, 255}
                                   : afterhours::Color{120, 120, 120, 255};
            float btnWidth = static_cast<float>(label.size()) * 9.0f + 24.0f;
            auto config = ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{w1280(btnWidth), h720(28)})
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(10),
                    .bottom = h720(4), .left = w1280(10)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(3)})
                .with_custom_background(btnBg)
                .with_custom_text_color(textCol)
                .with_font_size(h720(theme::layout::FONT_CHROME))
                .with_roundness(0.08f)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("toolbar_btn");
            config.disabled = !enabled;
            return static_cast<bool>(button(ctx, mk(toolbarBg.ent(), id), config));
        };

        auto toolbarSeparator = [&]() {
            int id = nextId++;
            div(ctx, mk(toolbarBg.ent(), id),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        w1280(theme::layout::TOOLBAR_SEP_WIDTH),
                        h720(theme::layout::TOOLBAR_SEP_HEIGHT)})
                    .with_margin(Margin{
                        .top = {}, .bottom = {},
                        .left = w1280(theme::layout::TOOLBAR_SEP_MARGIN),
                        .right = w1280(theme::layout::TOOLBAR_SEP_MARGIN)})
                    .with_custom_background(theme::BORDER)
                    .with_roundness(0.0f)
                    .with_debug_name("toolbar_sep"));
        };

        if (toolbarButton("Refresh", hasRepo)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }
        if (toolbarButton("Stage All", hasRepo && hasUnstaged)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }
        if (toolbarButton("Unstage All", hasRepo && hasStaged)) {
            if (hasRepo) {
                repoEntities[0].get().template get<RepoComponent>().refreshRequested = true;
            }
        }

        toolbarSeparator();

        if (toolbarButton("Commit", hasRepo && hasStaged)) {
            auto editorEntities = afterhours::EntityQuery({.force_merge = true})
                                      .whereHasComponent<CommitEditorComponent>()
                                      .gen();
            if (!editorEntities.empty()) {
                editorEntities[0].get().get<CommitEditorComponent>()
                    .commitRequested = true;
            }
        }
        if (toolbarButton("Push", hasRepo)) {}
        if (toolbarButton("Pull", hasRepo)) {}
        if (toolbarButton("Fetch", hasRepo)) {}

        toolbarSeparator();

        // Spacer to push branch selector right
        div(ctx, mk(toolbarBg.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{afterhours::ui::expand(), h720(1)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_spacer"));

        // Branch selector
        std::string branchLabel = branchName + " \xe2\x96\xbe";
        if (toolbarButton(branchLabel, hasRepo)) {
            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                auto& lc = lEntities[0].get().get<LayoutComponent>();
                lc.sidebarMode = LayoutComponent::SidebarMode::Refs;
                lc.sidebarVisible = true;
            }
        }
    }
};

}  // namespace ecs
