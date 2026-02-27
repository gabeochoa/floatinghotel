#pragma once

#include <algorithm>
#include <cmath>

#include <afterhours/src/logging.h>
#include "ui_imports.h"

namespace ecs {

// LayoutUpdateSystem: Recalculates all panel rectangles each frame based on
// current screen size, sidebar width, and commit log ratio.
struct LayoutUpdateSystem : afterhours::System<LayoutComponent> {
    void for_each_with(Entity& /*entity*/, LayoutComponent& layout,
                       float) override {
        int screenW = afterhours::graphics::get_screen_width();
        int screenH = afterhours::graphics::get_screen_height();
        float sw = static_cast<float>(screenW);
        float sh = static_cast<float>(screenH);

        auto rpxH = [sh](float design_px) {
            return resolve_to_pixels(h720(design_px), sh);
        };
        auto rpxW = [sw](float design_px) {
            return resolve_to_pixels(w1280(design_px), sw);
        };

        float tabStripH = std::max(rpxH(28.0f), 18.0f);
        float menuH = std::max(rpxH(static_cast<float>(theme::layout::MENU_BAR_HEIGHT)), 16.0f);
        float toolbarH = std::max(rpxH(static_cast<float>(theme::layout::TOOLBAR_HEIGHT)), 28.0f);
        float statusH = std::max(rpxH(static_cast<float>(theme::layout::STATUS_BAR_HEIGHT)), 16.0f);

        float actualTabStripH = tabStripH;

        layout.tabStrip = {0, 0, sw, actualTabStripH};
        layout.menuBar = {0, actualTabStripH, sw, menuH};

        float scaledSidebarW = rpxW(layout.sidebarWidth);
        float scaledSidebarMinW = rpxW(layout.sidebarMinWidth);

        float pctMinW = sw * theme::layout::SIDEBAR_MIN_PCT;
        if (pctMinW > scaledSidebarMinW) scaledSidebarMinW = pctMinW;

        float maxSidebarW = sw * 0.5f;
        scaledSidebarW = std::clamp(scaledSidebarW, std::min(scaledSidebarMinW, maxSidebarW), maxSidebarW);

        float dividerW = rpxW(4.0f);

        float topY = actualTabStripH + menuH;

        if (layout.sidebarVisible) {
            float sidebarToolbarH = std::max(rpxH(38.0f), 24.0f);
            layout.toolbar = {0, topY, scaledSidebarW, sidebarToolbarH};

            float sidebarContentY = topY + sidebarToolbarH;
            float sidebarContentH = std::max(sh - topY - sidebarToolbarH - statusH, 40.0f);
            layout.sidebar = {0, sidebarContentY, scaledSidebarW, sidebarContentH};

            float dividerH = rpxH(5.0f);
            float usableH = std::max(sidebarContentH - dividerH, 20.0f);
            float filesH = usableH * (1.0f - layout.commitLogRatio);
            float commitsH = usableH * layout.commitLogRatio;
            layout.sidebarFiles = {0, sidebarContentY, scaledSidebarW, filesH};
            layout.sidebarLog = {0, sidebarContentY + filesH, scaledSidebarW, commitsH};

            float scaledEditorH = rpxH(layout.commitEditorHeight);
            if (scaledEditorH > 0.0f) {
                layout.sidebarCommitEditor = {
                    0, sidebarContentY + sidebarContentH - scaledEditorH,
                    scaledSidebarW, scaledEditorH};
            } else {
                layout.sidebarCommitEditor = {0, 0, 0, 0};
            }

            float mainX = scaledSidebarW + dividerW;
            float mainW = std::max(sw - scaledSidebarW - dividerW, 20.0f);
            float mainContentY = topY;
            float mainContentH = std::max(sh - topY - statusH, 20.0f);

            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), mainContentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh;
                float mainH = mainContentH - logH;
                layout.mainContent = {mainX, mainContentY, mainW, mainH};
                layout.commandLog = {mainX, mainContentY + mainH, mainW, logH};
            } else {
                layout.mainContent = {mainX, mainContentY, mainW, mainContentH};
                layout.commandLog = {0, 0, 0, 0};
            }
        } else {
            layout.sidebar = {0, 0, 0, 0};
            layout.sidebarFiles = {0, 0, 0, 0};
            layout.sidebarLog = {0, 0, 0, 0};
            layout.sidebarCommitEditor = {0, 0, 0, 0};

            float contentY = topY + toolbarH;
            float contentH = std::max(sh - topY - toolbarH - statusH, 20.0f);
            layout.toolbar = {0, topY, sw, toolbarH};

            float mainX = 0;
            float mainW = sw;

            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), contentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh;
                float mainH = contentH - logH;
                layout.mainContent = {mainX, contentY, mainW, mainH};
                layout.commandLog = {mainX, contentY + mainH, mainW, logH};
            } else {
                layout.mainContent = {mainX, contentY, mainW, contentH};
                layout.commandLog = {0, 0, 0, 0};
            }
        }

        layout.statusBar = {0, sh - statusH, sw, statusH};
    }
};

}  // namespace ecs
