#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
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
#include "../ui/diff_renderer.h"
#include <chrono>
#include <thread>

#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../util/process.h"

struct SkipResizeCommand : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("resize")) return;
        cmd.consume();
    }
};

struct HandleReviewRoundtrip : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("roundtrip_review")) return;
        auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
        if (repo && review) {
            auto key = repo->repoPath + "::e2e-drafts";
            auto scope = review->storageScope;
            auto owner = review->storageRepoPath;
            review_store::save_review(key, *review);
            ecs::reset_review(*review);
            review->storageScope = scope;
            review->storageRepoPath = owner;
            review_store::load_review(key, *review);
            ecs::restore_draft_selection(*repo, *review);
            std::filesystem::remove(review_store::review_path(key, scope));
        }
        cmd.consume();
    }
};

struct HandleExpectReviewExport : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("expect_review_export")) return;
        if (!cmd.has_args(1)) { cmd.fail("expect_review_export requires text"); return; }
        std::string joined;
        for (const auto& arg : cmd.args) { if (!joined.empty()) joined += ' '; joined += arg; }
        std::istringstream input(joined);
        std::string expected;
        input >> std::quoted(expected);
        if (expected.empty()) { cmd.fail("expect_review_export requires nonempty text"); return; }
        auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        if (!repo) { cmd.fail("No active repository"); return; }
        std::ifstream exported(review_store::markdown_path(repo->repoPath, repo->currentBranch));
        std::string contents{std::istreambuf_iterator<char>(exported), std::istreambuf_iterator<char>()};
        if (contents.find(expected) == std::string::npos)
            cmd.fail("Export did not contain: " + expected);
        else cmd.consume();
    }
};

struct HandleMakeTestRepo : afterhours::System<afterhours::testing::PendingE2ECommand> {

    static constexpr const char* REPO_PATH = "/tmp/floatinghotel_test_repo";
    static constexpr const char* TEMPLATE_PATH = "/tmp/floatinghotel_test_template";

    bool ensure_template() {
        namespace fs = std::filesystem;
        // A bare `.git` existence check also passes for a half-created/corrupt
        // template (e.g. an interrupted setup), after which every make_test_repo
        // copies an empty repo and the whole flow suite fails. Require a real
        // repo with a resolvable HEAD; otherwise wipe and rebuild it.
        if (fs::exists(fs::path(TEMPLATE_PATH) / ".git")) {
            auto ok = run_process("", {"git", "-C", TEMPLATE_PATH,
                                       "rev-parse", "--verify", "HEAD"});
            if (ok.success()) return true;
            std::error_code ec;
            fs::remove_all(TEMPLATE_PATH, ec);
        }
        auto result = run_process("", {"bash", "scripts/setup_test_repo.sh"});
        return result.success();
    }

    bool reset_repo_fast() {
        namespace fs = std::filesystem;
        // Delete the old fixture outright. An earlier version renamed it to a
        // /tmp/fh_trash_* dir "to be fast" but never removed those, leaking
        // thousands of dirs over a suite's lifetime; the fixture is tiny (a
        // handful of files) so remove_all is plenty fast.
        if (fs::exists(REPO_PATH)) {
            // The previous script's git worker or FSEvents stream can still be
            // inside the tree for a moment, which makes remove_all fail with
            // "Directory not empty" under load. Retry briefly before giving up.
            std::error_code ec;
            for (int attempt = 0; attempt < 10; ++attempt) {
                ec.clear();
                fs::remove_all(REPO_PATH, ec);
                if (!ec && !fs::exists(REPO_PATH)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (ec || fs::exists(REPO_PATH)) {
                log_warn("make_test_repo: remove failed: {}", ec.message());
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

        auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
        if (layout) {
            ecs::reset_layout_defaults(*layout);
        }

        auto* tabStrip = ecs::find_singleton<ecs::TabStripComponent>();
        if (tabStrip && layout) {
            ecs::reset_tabs(*tabStrip, *layout);
        }

        auto* repoPtr = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        if (repoPtr) {
            auto& repo = *repoPtr;
            log_info("make_test_repo: switching from '{}' to '{}'", repo.repoPath, repoPath);
            repo.repoPath = repoPath;
            repo.reading = {};
            repo.navigation = {};
            repo.selectedFilePath.clear();
            repo.cachedFilePath.clear();
            repo.selectedCommitHash.clear();
            repo.ignoreWhitespace = false;
            repo.diffContext = 3;
            repo.fullFilePath.clear();
            repo.fullFileCacheKey.clear();
            repo.repoSearchOpen = false;
            repo.fileHistoryOpen = false;
            repo.fileHistoryFuture = {};
            repo.fileHistoryEntries.clear();
            repo.commitSearchOpen = false;
            repo.commitSearchFuture = {};
            repo.commitSearchEntries.clear();
            repo.commitSearchQuery = {};
            repo.comparisonOpen = false;
            repo.comparisonFuture = {};
            repo.comparisonScope.clear();
            repo.blameOpen = false;
            repo.blameFuture = {};
            repo.diffTargetFile.clear();
            repo.diffTargetFrames = 0;
            repo.repoSearchFuture = {};
            repo.repoSearchResults.clear();
            repo.fullFileTargetLine = 0;

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

            auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
            if (review) {
                ecs::reset_review(*review);
            }

            ui::diff_sel::reset();

            if (auto* editor = ecs::find_singleton<ecs::CommitEditorComponent,
                                                   ecs::ActiveTab>()) {
                ecs::reset_commit_editor(*editor);
            }

            if (auto* menu = ecs::find_singleton<ecs::MenuComponent>()) {
                ecs::reset_menus(*menu);
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
            auto filesResult = git::git_run(repoPath, {"ls-files", "--cached", "--others", "--exclude-standard", "-z"});
            repo.allFilePaths = git::parse_null_paths(filesResult.stdout_str());
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

        auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
        auto* tabStrip = ecs::find_singleton<ecs::TabStripComponent>();

        if (layout) {
            ecs::reset_layout_defaults(*layout);
        }
        if (tabStrip && layout) {
            ecs::reset_tabs(*tabStrip, *layout);
        }

        cmd.consume();
    }
};

struct HandleTabCommands : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed()) return;

        if (cmd.is("new_tab")) {
            auto* tabStrip = ecs::find_singleton<ecs::TabStripComponent>();
            auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
            if (!tabStrip || !layout) {
                cmd.fail("new_tab: missing TabStripComponent or LayoutComponent");
                return;
            }
            ecs::TabBarSystem::create_new_tab(*tabStrip, *layout);
            cmd.consume();
            return;
        }

        if (cmd.is("close_tab")) {
            auto* tabStripPtr = ecs::find_singleton<ecs::TabStripComponent>();
            auto* layoutPtr = ecs::find_singleton<ecs::LayoutComponent>();
            if (!tabStripPtr || !layoutPtr) {
                cmd.fail("close_tab: missing TabStripComponent or LayoutComponent");
                return;
            }
            auto& tabStrip = *tabStripPtr;
            auto& layout = *layoutPtr;
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
            auto* tabStrip = ecs::find_singleton<ecs::TabStripComponent>();
            auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
            if (!tabStrip || !layout) {
                cmd.fail("reset_tabs: missing TabStripComponent or LayoutComponent");
                return;
            }
            ecs::reset_tabs(*tabStrip, *layout);
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

        auto* repoPtr = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        if (!repoPtr) {
            cmd.fail("touch_file: no active repo");
            return;
        }

        auto& repo = *repoPtr;
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
