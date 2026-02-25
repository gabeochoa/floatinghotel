#pragma once

#include "tab_bar_system.h"

#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>

namespace ecs {

inline void reset_layout_defaults(LayoutComponent& layout) {
    layout.sidebarVisible = true;
    layout.commandLogVisible = false;
    layout.sidebarMode = LayoutComponent::SidebarMode::Changes;
    layout.fileViewMode = LayoutComponent::FileViewMode::Flat;
    layout.diffViewMode = LayoutComponent::DiffViewMode::Inline;
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
    auto scrollEntities = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<afterhours::ui::HasScrollView>().gen();
    for (auto& ref : scrollEntities) {
        auto& sv = ref.get().get<afterhours::ui::HasScrollView>();
        sv.scroll_offset = {0.0f, 0.0f};
    }

    auto toastEntities = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<afterhours::toast::Toast>().gen();
    for (auto& ref : toastEntities) {
        ref.get().cleanup = true;
    }

    auto* modalRoot = afterhours::EntityHelper::get_singleton_cmp<
        afterhours::modal::ModalRoot>();
    if (modalRoot) {
        modalRoot->modal_stack.clear();
    }

    auto ctxEntities = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<afterhours::ui::UIContext<InputAction>>().gen();
    if (!ctxEntities.empty()) {
        auto& ctx = ctxEntities[0].get().get<afterhours::ui::UIContext<InputAction>>();
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
