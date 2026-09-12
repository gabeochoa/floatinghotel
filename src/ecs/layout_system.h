#pragma once

#include <algorithm>
#include <cmath>

#include <afterhours/src/logging.h>
#include "ui_imports.h"
#include "../ui/zoom.h"
#include "../util/review_layout.h"

// Real OS window resize (Metal backend, defined in sokol_impl.mm) + test-mode
// flag (defined in main.cpp) so we never resize the window during e2e.
extern "C" void metal_set_window_size(int width, int height);
namespace app_state { extern bool testModeEnabled; }

namespace ecs {

// LayoutUpdateSystem: Recalculates all panel rectangles each frame based on
// current screen size, sidebar width, and commit log ratio.
struct LayoutUpdateSystem : afterhours::System<LayoutComponent> {
    void for_each_with(Entity& /*entity*/, LayoutComponent& layout,
                       float dt) override {
        int screenW = afterhours::graphics::get_screen_width();
        int screenH = afterhours::graphics::get_screen_height();
        float sw = static_cast<float>(screenW);
        float sh = static_cast<float>(screenH);

        // ---- Shelf state + smooth tray animation ----
        // The window width is tweened frame-by-frame; each step resizes the OS
        // window and lays out the whole UI at the animated width, so the window
        // and diff pane grow together (HTML mock's CSS-transition feel).
        // metal_set_window_size pins the Metal view's layer to the top-left, so
        // the sidebar stays pixel-stable through every step instead of stretching.
        {
            auto* shelfRepo = find_singleton<RepoComponent, ActiveTab>();
            bool hasRepoForShelf = shelfRepo && !shelfRepo->repoPath.empty();
            bool nothingSelected = hasRepoForShelf &&
                                   shelfRepo->selectedFilePath.empty() &&
                                   shelfRepo->selectedCommitHash.empty() && !layout.filePickerOpen && !shelfRepo->repoSearchOpen && !shelfRepo->fileHistoryOpen && !shelfRepo->commitSearchOpen && !shelfRepo->comparisonOpen;
            // While reviewing (in the ballroom) the diff pane shows every
            // working-tree file, so keep the shelf open even with no selection.
            auto* shelfReview = find_singleton<ReviewComponent, ActiveTab>();
            bool reviewingShelf = shelfReview && shelfReview->reviewing;
            layout.shelfCollapsed =
                layout.sidebarVisible && nothingSelected && !reviewingShelf;

            if (!app_state::testModeEnabled) {
                float collapsedW = (layout.sidebarWidth + 4.0f) * ui::zoom::get();
                // The window opened at the default shelf width; settings may
                // hold a different sidebar width. Square that once, silently,
                // rather than animating a correction the user never asked for.
                if (!layout.didInitialWidthSync) {
                    layout.didInitialWidthSync = true;
                    if (layout.shelfCollapsed && std::fabs(sw - collapsedW) > 1.0f) {
                        metal_set_window_size(static_cast<int>(collapsedW),
                                              static_cast<int>(sh));
                        sw = collapsedW;
                    }
                }
                if (layout.shelfCollapsed != layout.lastShelfCollapsed) {
                    if (layout.shelfCollapsed && sw > collapsedW + 40.f)
                        layout.expandedWidth = static_cast<int>(sw);
                    layout.animFrom = sw;
                    layout.animTarget =
                        layout.shelfCollapsed
                            ? collapsedW
                            : (layout.expandedWidth > 0
                                   ? static_cast<float>(layout.expandedWidth)
                                   : 1200.f);
                    layout.animT = 0.f;
                    layout.animating = true;
                    layout.lastShelfCollapsed = layout.shelfCollapsed;
                }
                if (layout.animating) {
                    layout.animT += (dt > 0.f ? dt : 0.016f) / 0.18f;
                    if (layout.animT >= 1.f) {
                        layout.animT = 1.f;
                        layout.animating = false;
                    }
                    float t = layout.animT;
                    float ease = t * t * (3.f - 2.f * t); // smoothstep
                    float curW = layout.animFrom +
                                 (layout.animTarget - layout.animFrom) * ease;
                    metal_set_window_size(static_cast<int>(curW),
                                          static_cast<int>(sh));
                    sw = curW; // lay out this frame at the animated width
                }
            }
        }

        const auto viewport = afterhours::ui::LayoutInfo::make(
            sw, sh, ui::zoom::get(), afterhours::ui::ScalingMode::Adaptive);
        const float width = viewport.logical_w;
        const float height = viewport.logical_h;
        const float statusH = std::min(26.f, height);
        const float availableH = height - statusH;
        const float tabStripH = std::min(28.f, availableH);
        const float menuH = std::min(26.f, availableH - tabStripH);
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        const float toolbarH = !layout.sidebarVisible && repo && !repo->reviewWorkspace
            ? std::min(42.f, availableH - tabStripH - menuH) : 0.f;
        const float topY = tabStripH + menuH + toolbarH;
        const float bodyH = availableH - topY;
        const bool sidebarOnly = layout.sidebarVisible && layout.shelfCollapsed;
        const auto sidebarState = !layout.sidebarVisible ? review_layout::Sidebar::Hidden
            : sidebarOnly ? review_layout::Sidebar::Collapsed
            : layout.animating ? review_layout::Sidebar::Animating
            : review_layout::Sidebar::Expanded;
        const float sidebarW = review_layout::sidebar_width(
            width, layout.sidebarWidth, layout.sidebarMinWidth, sidebarState);
        const float dividerW = sidebarW > 0.f && !sidebarOnly ? std::min(8.f, width - sidebarW) : 0.f;
        const float mainX = sidebarW + dividerW;

        layout.tabStrip = {0, 0, width, tabStripH};
        layout.menuBar = {0, tabStripH, sidebarOnly ? sidebarW : width, menuH};
        layout.toolbar = {0, tabStripH + menuH, width, toolbarH};
        layout.sidebar = sidebarW > 0.f ? LayoutComponent::Rect{0, topY, sidebarW, bodyH} : LayoutComponent::Rect{};
        const float sidebarDividerH = std::min(LayoutComponent::kCommitSplitterHeight, bodyH);
        const float usableSidebarH = bodyH - sidebarDividerH;
        const float commitsH = usableSidebarH * std::clamp(layout.commitLogRatio, 0.f, 1.f);
        const float filesH = usableSidebarH - commitsH;
        layout.sidebarFiles = {0, topY, sidebarW, filesH};
        layout.sidebarLog = {0, topY + filesH + sidebarDividerH, sidebarW, commitsH};
        layout.mainContent = sidebarOnly ? LayoutComponent::Rect{}
            : LayoutComponent::Rect{mainX, topY, width - mainX, bodyH};
        layout.commandLog = {};
        if (layout.commandLogVisible && !sidebarOnly) {
            const float logH = std::min(std::max(80.f, layout.commandLogHeight), bodyH * 0.6f);
            layout.mainContent.height -= logH;
            layout.commandLog = {mainX, topY + layout.mainContent.height, width - mainX, logH};
        }

        layout.contentTabs = {};
        auto* review = find_singleton<ReviewComponent, ActiveTab>();
        const bool hasContent = repo && (!repo->selectedCommitHash.empty() ||
            !repo->selectedFilePath.empty() || !repo->fullFilePath.empty() ||
            repo->comparisonOpen || (review && review->reviewing));
        if (hasContent && !sidebarOnly) {
            const float tabsH = std::min(40.f, layout.mainContent.height);
            layout.contentTabs = {layout.mainContent.x, layout.mainContent.y,
                                  layout.mainContent.width, tabsH};
            layout.mainContent.y += tabsH;
            layout.mainContent.height -= tabsH;
        }
        if (hasContent && !sidebarOnly) {
            const float inset = std::min(24.f, layout.mainContent.width * 0.04f);
            layout.mainContent.x += inset;
            layout.mainContent.width = std::max(0.f, layout.mainContent.width - inset * 2.f);
            const float topInset = std::min(8.f, layout.mainContent.height);
            layout.mainContent.y += topInset;
            layout.mainContent.height -= topInset;
        }
        layout.feedback = {};
        if (review && review->basketOpen && !review->comments.empty() &&
            layout.mainContent.width > 0) {
            const bool overlay = layout.mainContent.width < 720.f;
            const float feedbackW = std::min(300.f, layout.mainContent.width);
            if (!overlay) layout.mainContent.width -= feedbackW;
            layout.feedback = {layout.mainContent.x + layout.mainContent.width - (overlay ? feedbackW : 0.f),
                               layout.mainContent.y, feedbackW, layout.mainContent.height};
        }
        layout.statusBar = {0, height - statusH, width, statusH};
    }
};

}  // namespace ecs
