#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#ifdef __APPLE__
#include <copyfile.h>
#endif

#include <afterhours/src/logging.h>
#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>

#include "app_reset.h"
#include "components.h"
#include "query_helpers.h"
#include "tab_bar_system.h"
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../util/process.h"

struct SkipResizeCommand : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("resize")) return;
        cmd.consume();
    }
};

struct HandleMakeTestRepo : afterhours::System<afterhours::testing::PendingE2ECommand> {

    static constexpr const char* REPO_PATH = "/tmp/floatinghotel_test_repo";
    static constexpr const char* TEMPLATE_PATH = "/tmp/floatinghotel_test_template";

    bool ensure_template() {
        namespace fs = std::filesystem;
        if (fs::exists(fs::path(TEMPLATE_PATH) / ".git")) return true;
        auto result = run_process("", {"bash", "scripts/setup_test_repo.sh"});
        return result.success();
    }

    bool reset_repo_fast() {
        namespace fs = std::filesystem;
        static unsigned trash_counter = 0;
        std::string trashPath = std::string("/tmp/fh_trash_") +
            std::to_string(::getpid()) + "_" + std::to_string(++trash_counter);

        if (fs::exists(REPO_PATH)) {
            std::error_code ec;
            fs::rename(REPO_PATH, trashPath, ec);
            if (ec) {
                log_warn("make_test_repo: rename failed: {}", ec.message());
                return false;
            }
        }

#ifdef __APPLE__
        int ret = copyfile(TEMPLATE_PATH, REPO_PATH, nullptr,
                           COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE);
        if (ret != 0) {
            log_warn("make_test_repo: copyfile failed: {}", strerror(errno));
            return false;
        }
        return true;
#else
        std::error_code ec;
        fs::copy(TEMPLATE_PATH, REPO_PATH,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) {
            log_warn("make_test_repo: copy failed: {}", ec.message());
            return false;
        }
        return true;
#endif
    }

    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("make_test_repo")) return;

        if (!ensure_template()) {
            cmd.fail("make_test_repo: failed to create template");
            return;
        }

        if (!reset_repo_fast()) {
            cmd.fail("make_test_repo: failed to reset repo");
            return;
        }

        std::string repoPath = REPO_PATH;

        auto layoutQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::LayoutComponent>().gen();
        if (!layoutQ.empty()) {
            ecs::reset_layout_defaults(layoutQ[0].get().get<ecs::LayoutComponent>());
        }

        auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::TabStripComponent>().gen();
        if (!tabStripQ.empty() && !layoutQ.empty()) {
            ecs::reset_tabs(tabStripQ[0].get().get<ecs::TabStripComponent>(),
                            layoutQ[0].get().get<ecs::LayoutComponent>());
        }

        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<ecs::RepoComponent>()
                                .whereHasComponent<ecs::ActiveTab>()
                                .gen();
        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<ecs::RepoComponent>();
            log_info("make_test_repo: switching from '{}' to '{}'", repo.repoPath, repoPath);
            repo.repoPath = repoPath;
            repo.selectedFilePath.clear();
            repo.cachedFilePath.clear();
            repo.selectedCommitHash.clear();

            auto* detailCache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
            if (detailCache) {
                detailCache->cachedCommitHash.clear();
                detailCache->commitDetailDiff.clear();
                detailCache->commitDetailBody.clear();
                detailCache->commitDetailAuthorEmail.clear();
                detailCache->commitDetailParents.clear();
            }

            auto* branchDialog = ecs::find_singleton<ecs::BranchDialogState, ecs::ActiveTab>();
            if (branchDialog) {
                branchDialog->showNewBranchDialog = false;
                branchDialog->newBranchName.clear();
                branchDialog->showDeleteBranchDialog = false;
                branchDialog->deleteBranchName.clear();
                branchDialog->showForceDeleteDialog = false;
            }

            auto editorEntities = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::CommitEditorComponent>()
                .whereHasComponent<ecs::ActiveTab>()
                .gen();
            if (!editorEntities.empty()) {
                ecs::reset_commit_editor(editorEntities[0].get().get<ecs::CommitEditorComponent>());
            }

            auto menuEntities = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::MenuComponent>()
                .gen();
            if (!menuEntities.empty()) {
                ecs::reset_menus(menuEntities[0].get().get<ecs::MenuComponent>());
            }

            repo.refreshRequested = true;
            repo.isRefreshing = true;

            auto statusResult = git::git_status(repoPath);
            if (statusResult.success()) {
                auto parsed = git::parse_status(statusResult.stdout_str());
                repo.currentBranch = parsed.branchName;
                repo.isDetachedHead = parsed.isDetachedHead;
                repo.aheadCount = parsed.aheadCount;
                repo.behindCount = parsed.behindCount;
                repo.stagedFiles = std::move(parsed.stagedFiles);
                repo.unstagedFiles = std::move(parsed.unstagedFiles);
                repo.untrackedFiles = std::move(parsed.untrackedFiles);
                repo.isDirty = !repo.stagedFiles.empty() ||
                               !repo.unstagedFiles.empty() ||
                               !repo.untrackedFiles.empty();
            }

            auto logResult = git::git_log(repoPath, 100, 0);
            if (logResult.success()) {
                repo.commitLog = git::parse_log(logResult.stdout_str());
                repo.commitLogLoaded = static_cast<int>(repo.commitLog.size());
                repo.commitLogHasMore = (repo.commitLogLoaded >= 100);
            }

            auto diffResult = git::git_diff(repoPath);
            if (diffResult.success()) {
                repo.currentDiff = git::parse_diff(diffResult.stdout_str());
            }

            auto branchResult = git::git_branch_list(repoPath);
            if (branchResult.success()) {
                repo.branches = git::parse_branch_list(branchResult.stdout_str());
            }

            auto headResult = git::git_rev_parse_head(repoPath);
            if (headResult.success()) {
                repo.headCommitHash = headResult.stdout_str();
                while (!repo.headCommitHash.empty() &&
                       (repo.headCommitHash.back() == '\n' ||
                        repo.headCommitHash.back() == '\r')) {
                    repo.headCommitHash.pop_back();
                }
            }

            repo.isRefreshing = false;
            repo.refreshRequested = false;
            repo.hasLoadedOnce = true;
            repo.repoVersion++;
        } else {
            log_warn("make_test_repo: no RepoComponent entity found!");
        }

        ecs::reset_ui_transient_state();

        log_info("make_test_repo: done, path={}", repoPath);
        cmd.consume();
    }
};

struct HandleResetUI : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("reset_ui")) return;

        auto layoutQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::LayoutComponent>().gen();
        auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::TabStripComponent>().gen();

        if (!layoutQ.empty()) {
            ecs::reset_layout_defaults(layoutQ[0].get().get<ecs::LayoutComponent>());
        }
        if (!tabStripQ.empty() && !layoutQ.empty()) {
            ecs::reset_tabs(tabStripQ[0].get().get<ecs::TabStripComponent>(),
                            layoutQ[0].get().get<ecs::LayoutComponent>());
        }

        cmd.consume();
    }
};

struct HandleTabCommands : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed()) return;

        if (cmd.is("new_tab")) {
            auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::TabStripComponent>().gen();
            auto layoutQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (tabStripQ.empty() || layoutQ.empty()) {
                cmd.fail("new_tab: missing TabStripComponent or LayoutComponent");
                return;
            }
            auto& tabStrip = tabStripQ[0].get().get<ecs::TabStripComponent>();
            auto& layout = layoutQ[0].get().get<ecs::LayoutComponent>();
            ecs::TabBarSystem::create_new_tab(tabStrip, layout);
            cmd.consume();
            return;
        }

        if (cmd.is("close_tab")) {
            auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::TabStripComponent>().gen();
            auto layoutQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (tabStripQ.empty() || layoutQ.empty()) {
                cmd.fail("close_tab: missing TabStripComponent or LayoutComponent");
                return;
            }
            auto& tabStrip = tabStripQ[0].get().get<ecs::TabStripComponent>();
            auto& layout = layoutQ[0].get().get<ecs::LayoutComponent>();
            if (tabStrip.tabOrder.size() <= 1) {
                cmd.fail("close_tab: cannot close the last tab");
                return;
            }
            for (size_t i = 0; i < tabStrip.tabOrder.size(); ++i) {
                auto opt = afterhours::EntityHelper::getEntityForID(tabStrip.tabOrder[i]);
                if (opt.valid() && opt->has<ecs::ActiveTab>()) {
                    ecs::TabBarSystem::close_tab(tabStrip, tabStrip.tabOrder[i], i, true, layout);
                    break;
                }
            }
            cmd.consume();
            return;
        }

        if (cmd.is("reset_tabs")) {
            auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::TabStripComponent>().gen();
            auto layoutQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (tabStripQ.empty() || layoutQ.empty()) {
                cmd.fail("reset_tabs: missing TabStripComponent or LayoutComponent");
                return;
            }
            ecs::reset_tabs(tabStripQ[0].get().get<ecs::TabStripComponent>(),
                            layoutQ[0].get().get<ecs::LayoutComponent>());
            cmd.consume();
            return;
        }
    }
};

struct HandleTouchFile : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("touch_file")) return;
        if (!cmd.has_args(1)) {
            cmd.fail("touch_file requires a filename argument");
            return;
        }

        auto repoQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::RepoComponent>()
            .whereHasComponent<ecs::ActiveTab>().gen();
        if (repoQ.empty()) {
            cmd.fail("touch_file: no active repo");
            return;
        }

        auto& repo = repoQ[0].get().get<ecs::RepoComponent>();
        std::filesystem::path filePath = std::filesystem::path(repo.repoPath) / cmd.args[0];

        std::ofstream ofs(filePath, std::ios::app);
        if (!ofs) {
            cmd.fail("touch_file: could not open " + filePath.string());
            return;
        }
        ofs << "# edited by e2e test\n";
        ofs.close();

        log_info("touch_file: wrote to {}", filePath.string());
        cmd.consume();
    }
};
