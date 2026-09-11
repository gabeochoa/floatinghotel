#pragma once

#include "query_helpers.h"
#include "tab_bar_system.h"

#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>

namespace ecs {

inline void reset_layout_defaults(LayoutComponent& layout) {
    layout.filePickerOpen = false;
    layout.filePickerFocus = false;
    layout.filePickerQuery.clear();
    layout.filePickerCacheKey.clear();
    layout.filePickerIndex = 0;
    layout.sidebarVisible = true;
    layout.commandLogVisible = false;
    layout.sidebarMode = LayoutComponent::SidebarMode::Changes;
    layout.reviewTab = LayoutComponent::ReviewTab::ToReview;
    layout.fileViewMode = LayoutComponent::FileViewMode::Flat;
    layout.diffViewMode = LayoutComponent::DiffViewMode::Inline;
    layout.commitMetadataExpanded = false;
    layout.shortcutsOpen = false;
    layout.diffFindOpen = false;
    layout.visibleWhitespace = false;
    layout.diffFindQuery.clear();
    layout.diffFindIndex = 0;
    layout.diffFindNavigate = 0;
    layout.diffFindFocus = false;
    // A script that drags the divider otherwise hands its last width to every
    // script after it, and the menu bar collapses into "More" at narrow
    // widths -- so the leak reads as "No UI with text: View" ten files later.
    layout.sidebarWidth = LayoutComponent::kDefaultSidebarWidth;
}

inline void reset_tabs(TabStripComponent& tabStrip, LayoutComponent& layout) {
    while (tabStrip.tabOrder.size() > 1) {
        auto lastId = tabStrip.tabOrder.back();
        auto lastIdx = tabStrip.tabOrder.size() - 1;
        TabBarSystem::close_tab(tabStrip, lastId, lastIdx, false, layout);
    }
    if (!tabStrip.tabOrder.empty()) {
        auto firstOpt = afterhours::EntityHelper::getEntityForID(tabStrip.tabOrder[0]);
        if (firstOpt.valid() && !firstOpt->has<ActiveTab>()) {
            firstOpt->addComponent<ActiveTab>();
        }
    }
}

inline void reset_commit_editor(CommitEditorComponent& editor) {
    editor.subject.clear();
    editor.body.clear();
    editor.isVisible = false;
    editor.isAmend = false;
    editor.commitRequested = false;
    editor.showUnstagedDialog = false;
    editor.rememberChoice = false;
}

inline void reset_menus(MenuComponent& menu) {
    menu.activeMenuIndex = -1;
    menu.pendingDialog = MenuComponent::PendingDialog::None;
    menu.pendingToast.clear();
}

inline void reset_ui_transient_state() {
    // for_each_stream: these only flip fields, so there is no reason to
    // materialize a ref vector first.
    afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<afterhours::ui::HasScrollView>()
        .for_each_stream([](afterhours::Entity& e) {
            // scroll_offset eases toward scroll_target every frame, so clearing
            // only the offset lets the easing drag the view straight back to
            // wherever the previous script left it. Clear all three, the way a
            // scrollbar drag does, so the reset is authoritative.
            auto& sv = e.get<afterhours::ui::HasScrollView>();
            sv.scroll_offset = {0.0f, 0.0f};
            sv.scroll_target = {0.0f, 0.0f};
            sv.last_eased_offset = {0.0f, 0.0f};
            sv.anchor_child = -1;
        });

    afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<afterhours::toast::Toast>()
        .for_each_stream([](afterhours::Entity& e) { e.cleanup = true; });

    auto* modalRoot = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::modal::ModalRoot>();
    if (modalRoot) {
        modalRoot->modal_stack.clear();
    }

    auto* ctxPtr = find_singleton<afterhours::ui::UIContext<InputAction>>();
    if (ctxPtr) {
        auto& ctx = *ctxPtr;
        ctx.hot_id = ctx.ROOT;
        ctx.prev_hot_id = ctx.ROOT;
        ctx.focus_id = ctx.ROOT;
        ctx.visual_focus_id = ctx.ROOT;
        ctx.active_id = ctx.ROOT;
        ctx.prev_active_id = ctx.ROOT;
        ctx.last_processed = ctx.ROOT;
        ctx.input_gates.clear();
    }

    afterhours::testing::test_input::reset_all();
}

} // namespace ecs
