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
    repo.pushDialogOpen = true;
    repo.pushRepository = repo.repoPath;
    repo.pushDestination = {};
    repo.pushDestinationFuture = async_work::launch([path = repo.repoPath, remote](std::stop_token stop) {
        return git::read_push_destination(path, remote, stop);
    }, async_work::Priority::Foreground, git::PushDestination{.error = "Reader queue is full; reopen Push"});
}

struct RepositoryActionsSystem : afterhours::System<UIContext<InputAction>> {
    static bool render_relink(UIContext<InputAction>& ctx) {
        auto* layout = find_singleton<LayoutComponent>();
        if (!layout || !layout->relinkOpen) {
            bool closed = false;
            afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586400), closed, afterhours::ModalConfig{});
            ctx.remove_input_gate("relink_dialog");
            return false;
        }
        const auto key = layout->relinkOld + "\n" + layout->relinkNew;
        if (layout->relinkFuture.valid() && layout->relinkFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = layout->relinkFuture.get();
            if (layout->relinkRequest == key) {
                layout->relinkError = result.success() ? review_store::copy_repository_reviews(layout->relinkOld, layout->relinkNew) : result.stderr_str();
                if (layout->relinkError.empty()) {
                    if (!Settings::get().relink_repository(layout->relinkOld, layout->relinkNew)) layout->relinkError = "Could not save the new repository location; existing data was retained";
                    else {
                        afterhours::EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent>().for_each_stream([&](Entity& entity) {
                            auto& repo = entity.get<RepoComponent>();
                            if (repo.repoPath != layout->relinkOld) return;
                            navigation::release_source(repo);
                            repo.repoPath = layout->relinkNew;
                            repo.readingSessionPath = layout->relinkNew;
                            ++repo.dataGeneration;
                            repo.refreshRequested = true;
                            if (entity.has<ReviewComponent>()) entity.get<ReviewComponent>().storageRepoPath = repo.repoPath;
                        });
                        layout->relinkOpen = false;
                        ctx.remove_input_gate("relink_dialog");
                        return true;
                    }
                }
            }
        }
        auto modal = afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586400), layout->relinkOpen,
            afterhours::ModalConfig{}.with_size(pixels(std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f)), pixels(260)).with_title("Relink moved repository").with_backdrop_color({0, 0, 0, 0}));
        if (!modal) { layout->relinkFuture = {}; ctx.remove_input_gate("relink_dialog"); return true; }
        ui::dialog_backdrop(ctx, 586300);
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element = -1;
        modal.cmp().absolute_pos_x = (ctx.screen_width - std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f) * ui::zoom::get()) * .5f;
        modal.cmp().absolute_pos_y = std::max(8.f, (ctx.screen_height - 380.f * ui::zoom::get()) * .5f);
        const auto id = modal.ent().id;
        ctx.add_input_gate("relink_dialog", [id](afterhours::EntityID target) { return afterhours::modal::detail::is_entity_in_tree(id, target); });
        auto body = div(ctx, mk(modal.ent(), 20), ComponentConfig{}.with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_gap(pixels(8)).with_render_layer(1001));
        div(ctx, mk(body.ent(), 0), preset::BodyText("Previous: " + layout->relinkOld).with_size(ComponentSize{percent(1.f), pixels(28)})
            .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
        afterhours::text_input::text_input(ctx, mk(body.ent(), 1), layout->relinkNew,
            ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("relink_repository_path"));
        div(ctx, mk(body.ent(), 2), preset::BodyText(layout->relinkFuture.valid() ? "Verifying repository identity..." : layout->relinkError)
            .with_size(ComponentSize{percent(1.f), pixels(44)}).with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("relink_repository_status"));
        if (button(ctx, mk(body.ent(), 3), preset::Button("Relink and retain reviews", !layout->relinkFuture.valid() && !layout->relinkNew.empty())
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_debug_name("relink_repository_confirm"))) {
            std::error_code error;
            const auto canonical = std::filesystem::canonical(layout->relinkNew, error);
            if (error) { layout->relinkError = "The new directory is unavailable"; return true; }
            layout->relinkNew = canonical.string();
            bool saved = true;
            afterhours::EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent, ReviewComponent>().for_each_stream([&](Entity& entity) {
                const auto& source = entity.get<RepoComponent>();
                if (source.repoPath != layout->relinkOld) return;
                Settings::get().set_reading_session(source.repoPath, reading::save_session(source.workspace()));
                const auto& review = entity.get<ReviewComponent>();
                if (review.dirty) saved &= review_store::save_review(source.repoPath, review);
            });
            if (!saved || !Settings::get().saveError.empty()) { layout->relinkError = "Save the existing reading state before relinking"; return true; }
            const auto remembered = Settings::get().repository_identity(layout->relinkOld);
            if (remembered.empty()) { layout->relinkError = "No saved commit identity is available for this repository"; return true; }
            layout->relinkRequest = layout->relinkOld + "\n" + layout->relinkNew;
            layout->relinkFuture = async_work::launch([path = layout->relinkNew, remembered](std::stop_token stop) {
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
        if (!repo || !repo->pushDialogOpen) {
            bool closed = false;
            afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586300), closed, afterhours::ModalConfig{});
            ctx.remove_input_gate("push_dialog");
            return;
        }
        if (repo->pushRepository != repo->repoPath || repo->reviewWorkspace) {
            repo->pushDialogOpen = false;
            repo->pushDestinationFuture = {};
            return;
        }
        if (repo->pushDestinationFuture.valid() && repo->pushDestinationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            repo->pushDestination = repo->pushDestinationFuture.get();
        auto& root = ui_imm::getUIRootEntity();
        auto modal = afterhours::modal::detail::modal_impl(ctx, ui::dialog_parent(586300), repo->pushDialogOpen,
            afterhours::ModalConfig{}.with_size(pixels(std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f)), pixels(380)).with_title("Push destination").with_backdrop_color({0, 0, 0, 0}));
        if (!modal) { repo->pushDestinationFuture = {}; ctx.remove_input_gate("push_dialog"); return; }
        ui::dialog_backdrop(ctx, 586300);
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element = -1;
        modal.cmp().absolute_pos_x = (ctx.screen_width - std::min(560.f, ctx.screen_width / ui::zoom::get() - 32.f) * ui::zoom::get()) * .5f;
        modal.cmp().absolute_pos_y = std::max(8.f, (ctx.screen_height - 380.f * ui::zoom::get()) * .5f);
        const auto id = modal.ent().id;
        ctx.add_input_gate("push_dialog", [id](afterhours::EntityID target) { return afterhours::modal::detail::is_entity_in_tree(id, target); });
        auto& destination = repo->pushDestination;
        auto body = div(ctx, mk(modal.ent(), 20), ComponentConfig{}.with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_gap(pixels(8)).with_padding(Padding{.top = pixels(8), .right = pixels(12), .bottom = pixels(8), .left = pixels(12)}).with_render_layer(1001));
        auto text = [&](int index, const std::string& value) {
            div(ctx, mk(body.ent(), index), preset::BodyText(value).with_size(ComponentSize{percent(1.f), pixels(26)})
                .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_tooltip(value));
        };
        text(0, repo->pushDestinationFuture.valid() ? "Reading push destination..." : "From local branch: " + destination.branch);
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
                !repo->pushDestinationFuture.valid() && destination.error.empty() && !destination.destination.empty())
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_debug_name("push_confirm_destination"))) {
            const auto target = destination;
            enqueue_network_op("Push to " + target.remote + "/" + target.destination,
                async_work::launch([path = repo->repoPath, target](std::stop_token) { return git::push_explicit(path, target); },
                    async_work::Priority::Foreground, git::GitResult{{"", "Operation queue is full; retry Push", -1}}, false));
            repo->pushDialogOpen = false;
            ctx.remove_input_gate("push_dialog");
        }
    }
};

}
