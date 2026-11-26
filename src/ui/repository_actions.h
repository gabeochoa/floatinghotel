#pragma once

#include "../ecs/ui_imports.h"
#include "../ecs/network_ops_system.h"
#include "focus.h"
#include "../review_store.h"
#include "../settings.h"
#include "../util/navigation.h"
#include <afterhours/src/plugins/modal.h>

namespace ecs {

inline void open_push_dialog(RepoComponent& repo, std::string remote = {}) {
    if (repo.reviewWorkspace || repo.repoPath.empty()) return;
    auto* layout = find_singleton<LayoutComponent>();
    if (!layout) return;
    layout->open_modal(reading::focus::Popup::PushDialog);
    repo.pushDialog.repository = repo.repoPath;
    repo.pushDialog.destination = {};
    repo.pushDialog.future = async_work::launch([path = repo.repoPath, remote](std::stop_token stop) {
        return git::read_push_destination(path, remote, stop);
    }, async_work::Priority::Foreground, git::PushDestination{.error = "Reader queue is full; reopen Push"});
}

struct RepositoryActionsSystem : afterhours::System<UIContext<InputAction>> {
    static bool render_relink(UIContext<InputAction>& ctx) {
        auto* layout = find_singleton<LayoutComponent>();
        if (!layout || !layout->modal_is(reading::focus::Popup::RelinkDialog)) {
            bool closed = false;
            afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586400), closed, afterhours::ModalConfig{});
            ctx.remove_input_gate("relink_dialog");
            return false;
        }
        const auto key = layout->relinkDialog.oldPath + "\n" + layout->relinkDialog.newPath;
        if (layout->relinkDialog.future.valid() && layout->relinkDialog.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = layout->relinkDialog.future.get();
            if (layout->relinkDialog.request == key) {
                layout->relinkDialog.error = result.success() ? review_store::copy_repository_reviews(layout->relinkDialog.oldPath, layout->relinkDialog.newPath) : result.stderr_str();
                if (layout->relinkDialog.error.empty()) {
                    if (!Settings::get().relink_repository(layout->relinkDialog.oldPath, layout->relinkDialog.newPath)) layout->relinkDialog.error = "Could not save the new repository location; existing data was retained";
                    else {
                        afterhours::EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent>().for_each_stream([&](Entity& entity) {
                            auto& repo = entity.get<RepoComponent>();
                            if (repo.repoPath != layout->relinkDialog.oldPath) return;
                            navigation::release_source(repo);
                            repo.repoPath = layout->relinkDialog.newPath;
                            repo.readingSessionPath = layout->relinkDialog.newPath;
                            ++repo.dataGeneration;
                            repo.request_refresh();
                            if (entity.has<ReviewComponent>()) entity.get<ReviewComponent>().storageRepoPath = repo.repoPath;
                        });
                        layout->close_modal(reading::focus::Popup::RelinkDialog);
                        ctx.remove_input_gate("relink_dialog");
                        return true;
                    }
                }
            }
        }
        bool relinkOpen = layout->modal_is(reading::focus::Popup::RelinkDialog);
        auto modal = afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586400), relinkOpen,
            afterhours::ModalConfig{}.with_size(pixels(std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f)), pixels(260)).with_title("Relink moved repository").with_backdrop_color({0, 0, 0, 0}));
        if (!relinkOpen) layout->close_modal(reading::focus::Popup::RelinkDialog);
        if (!modal) { layout->relinkDialog.future = {}; ctx.remove_input_gate("relink_dialog"); return true; }
        ui::dialog_backdrop(ctx, 586300);
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element =
            afterhours::EntityHandle::invalid();
        const auto id = modal.ent().id;
        ctx.add_input_gate("relink_dialog", [id](afterhours::EntityID target) { return afterhours::modal::detail::is_entity_in_tree(id, target); });
        auto body = div(ctx, mk(modal.ent(), 20), ComponentConfig{}.with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_gap(pixels(8)).with_render_layer(1001));
        div(ctx, mk(body.ent(), 0), preset::BodyText("Previous: " + layout->relinkDialog.oldPath).with_size(ComponentSize{percent(1.f), pixels(28)})
            .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
        afterhours::text_input::text_input(ctx, mk(body.ent(), 1), layout->relinkDialog.newPath,
            ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("relink_repository_path"));
        div(ctx, mk(body.ent(), 2), preset::BodyText(layout->relinkDialog.future.valid() ? "Verifying repository identity..." : layout->relinkDialog.error)
            .with_size(ComponentSize{percent(1.f), pixels(44)}).with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("relink_repository_status"));
        if (button(ctx, mk(body.ent(), 3), preset::Button("Relink and retain reviews", !layout->relinkDialog.future.valid() && !layout->relinkDialog.newPath.empty())
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_debug_name("relink_repository_confirm"))) {
            std::error_code error;
            const auto canonical = std::filesystem::canonical(layout->relinkDialog.newPath, error);
            if (error) { layout->relinkDialog.error = "The new directory is unavailable"; return true; }
            layout->relinkDialog.newPath = canonical.string();
            bool saved = true;
            afterhours::EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent, ReviewComponent>().for_each_stream([&](Entity& entity) {
                const auto& source = entity.get<RepoComponent>();
                if (source.repoPath != layout->relinkDialog.oldPath) return;
                Settings::get().set_reading_session(source.repoPath, reading::save_session(source.workspace()));
                const auto& review = entity.get<ReviewComponent>();
                if (review.dirty()) saved &= review_store::save_review(source.repoPath, review);
            });
            if (!saved || !Settings::get().saveError.empty()) { layout->relinkDialog.error = "Save the existing reading state before relinking"; return true; }
            const auto remembered = Settings::get().repository_identity(layout->relinkDialog.oldPath);
            if (remembered.empty()) { layout->relinkDialog.error = "No saved commit identity is available for this repository"; return true; }
            layout->relinkDialog.request = layout->relinkDialog.oldPath + "\n" + layout->relinkDialog.newPath;
            layout->relinkDialog.future = async_work::launch([path = layout->relinkDialog.newPath, remembered](std::stop_token stop) {
                auto root = git::git_run(path, {"rev-parse", "--show-toplevel"}, stop);
                if (!root.success() || git::catalog::trim_line(root.stdout_str()) != path) return git::GitResult{{"", "Select the repository's root directory", -1}};
                auto identity = git::git_run(path, {"cat-file", "-e", remembered + "^{commit}"}, stop);
                if (!identity.success()) identity.raw.stderr_str = "This location does not contain the repository's remembered commit";
                return identity;
            }, async_work::Priority::Foreground, git::GitResult{{"", "Reader queue is full; retry", -1}});
        }
        return true;
    }

    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        if (render_relink(ctx)) return;
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        auto* layout = find_singleton<LayoutComponent>();
        if (!repo || !layout || !layout->modal_is(reading::focus::Popup::PushDialog)) {
            bool closed = false;
            afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586300), closed, afterhours::ModalConfig{});
            ctx.remove_input_gate("push_dialog");
            return;
        }
        if (repo->pushDialog.repository != repo->repoPath || repo->reviewWorkspace) {
            layout->close_modal(reading::focus::Popup::PushDialog);
            repo->pushDialog.future = {};
            return;
        }
        if (repo->pushDialog.future.valid() && repo->pushDialog.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            repo->pushDialog.destination = repo->pushDialog.future.get();
        bool pushOpen = layout->modal_is(reading::focus::Popup::PushDialog);
        auto modal = afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586300), pushOpen,
            afterhours::ModalConfig{}.with_size(pixels(std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f)), pixels(380)).with_title("Push destination").with_backdrop_color({0, 0, 0, 0}));
        if (!pushOpen) layout->close_modal(reading::focus::Popup::PushDialog);
        if (!modal) { repo->pushDialog.future = {}; ctx.remove_input_gate("push_dialog"); return; }
        ui::dialog_backdrop(ctx, 586300);
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element =
            afterhours::EntityHandle::invalid();
        const auto id = modal.ent().id;
        ctx.add_input_gate("push_dialog", [id](afterhours::EntityID target) { return afterhours::modal::detail::is_entity_in_tree(id, target); });
        auto& destination = repo->pushDialog.destination;
        auto body = div(ctx, mk(modal.ent(), 20), ComponentConfig{}.with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_gap(pixels(8)).with_padding(Padding{.top = pixels(8), .right = pixels(12), .bottom = pixels(8), .left = pixels(12)}).with_render_layer(1001));
        auto text = [&](int index, const std::string& value) {
            div(ctx, mk(body.ent(), index), preset::BodyText(value).with_size(ComponentSize{percent(1.f), pixels(26)})
                .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_tooltip(value));
        };
        text(0, repo->pushDialog.future.valid() ? "Reading push destination..." : "From local branch: " + destination.branch);
        if (button(ctx, mk(body.ent(), 1), preset::Button(destination.remote.empty() ? "Choose remote" : "Remote: " + destination.remote)
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_debug_name("push_choose_remote"))) {
            std::vector<ui::ContextMenuItem> items;
            for (const auto& remote : destination.remotes) items.push_back(ui::ContextMenuItem::item(remote, [remote] {
                if (auto* active = find_singleton<RepoComponent, ActiveTab>()) open_push_dialog(*active, remote);
            }));
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
        }
        text(2, "Fetch URL: " + destination.fetchUrl);
        text(3, "Push URL: " + destination.pushUrl);
        text(4, "Destination branch");
        afterhours::text_input::text_input(ctx, mk(body.ent(), 5), destination.destination,
            ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(30)}).with_debug_name("push_destination_branch"));
        text(6, destination.error);
        if (button(ctx, mk(body.ent(), 7), preset::Button("Push to " + destination.remote + "/" + destination.destination,
                !repo->pushDialog.future.valid() && destination.error.empty() && !destination.destination.empty())
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_debug_name("push_confirm_destination"))) {
            const auto target = destination;
            enqueue_network_op("Push to " + target.remote + "/" + target.destination,
                async_work::launch([path = repo->repoPath, target](std::stop_token stop) { return git::push_explicit(path, target, stop); },
                    async_work::Priority::Foreground, git::GitResult{{"", "Operation queue is full; retry Push", -1}}));
            layout->close_modal(reading::focus::Popup::PushDialog);
            ctx.remove_input_gate("push_dialog");
        }
    }
};

}
