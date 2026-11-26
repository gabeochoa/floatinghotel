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
#include "agent_panel.h"
#include "../preload.h"
#include "layout_system.h"
#include "components.h"
#include "query_helpers.h"
#include "tab_bar_system.h"
#include "../ui/diff_renderer.h"
#include <chrono>
#include <thread>

#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../util/process.h"
#include "../platform/native_menu.h"

struct HandleSaveWindowState : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("save_window_state")) return;
        auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
        if (!std::getenv("FH_TEST_SETTINGS_DIR") || !layout) {
            cmd.fail("save_window_state requires isolated test settings");
            return;
        }
        ecs::remember_window_size(*layout, afterhours::graphics::get_screen_width(), afterhours::graphics::get_screen_height());
        if (const auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>(); repo && !repo->repoPath.empty())
            Settings::get().set_reading_session(repo->repoPath, reading::save_session(repo->workspace()));
        Settings::get().write_save_file();
        cmd.consume();
    }
};

struct SkipResizeCommand : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("resize")) return;
        cmd.consume();
    }
};

// Text argument from args[start] on, space-joined. The script parser
// already unquotes quoted arguments into single args.
inline std::string e2e_joined_arg(const std::vector<std::string>& args, size_t start) {
    std::string joined;
    for (size_t i = start; i < args.size(); ++i) { if (!joined.empty()) joined += ' '; joined += args[i]; }
    return joined;
}

struct HandleShowToast : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("show_toast")) return;
        if (!cmd.has_args(2)) { cmd.fail("show_toast requires a level and message"); return; }
        const std::string message = e2e_joined_arg(cmd.args, 1);
        auto& ctx = afterhours::EntityHelper::get_singleton_cmp_enforce<ui_imm::UIContextType>();
        if (cmd.args[0] == "info") afterhours::toast::send_info(ctx, message);
        else if (cmd.args[0] == "success") afterhours::toast::send_success(ctx, message);
        else if (cmd.args[0] == "warning") afterhours::toast::send_warning(ctx, message);
        else if (cmd.args[0] == "error") afterhours::toast::send_error(ctx, message);
        else { cmd.fail("Unknown toast level"); return; }
        cmd.consume();
    }
};

struct HandleNativeMenuAction : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("native_menu_action")) return;
        const std::string title = e2e_joined_arg(cmd.args, 0);
        if (!native_menu::activate_for_test(title)) cmd.fail("Native menu item is unavailable: " + title);
        else cmd.consume();
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
            navigation::restore_draft(*repo, *review);
            std::filesystem::remove(review_store::review_path(key, scope));
        }
        cmd.consume();
    }
};

struct HandleExpectReviewExport : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("expect_review_export")) return;
        if (!cmd.has_args(1)) { cmd.fail("expect_review_export requires text"); return; }
        const std::string expected = e2e_joined_arg(cmd.args, 0);
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
            navigation::reset(repo);
            repo.ignoreWhitespace = false;
            repo.diffContext = 3;
            repo.fullFileLoad.invalidate_key();
            repo.fullFileMarkdownPreview = false;
            repo.markdownLink = {};
            if (theme::current_theme_name != theme::ThemeName::Dark) apply_ui_theme(false);
            repo.close_search();
            repo.repoSearchOptionsOpen = false;
            repo.repoSearchChangedOnly = false;
            repo.repoSearchIncludeGlob.clear();
            repo.repoSearchExcludeGlob.clear();
            repo.fileHistoryOpen = false;
            repo.fileHistory.invalidate();
            repo.fileHistoryEntries.clear();
            repo.commitSearchOpen = false;
            repo.commitSearch.invalidate();
            repo.commitSearchEntries.clear();
            repo.commitSearchQuery = {};
            repo.comparisonFuture = {};
            repo.comparisonLoadedScope.clear();
            repo.close_comparison();
            repo.blameOpen = false;
            if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) layout->modal.reset();
            repo.blame.cancel();
            repo.blameGutter.cancel();
            repo.diffTargetFrames = 0;
            repo.repoSearchFuture = {};
            repo.repoSearchPreviewFuture = {};
            repo.repoSearchResults.clear();
            repo.repoSearchQuery.clear();
            repo.repoSearchSubmittedQuery.clear();
            repo.repoSearchMatching = {};
            repo.repoSearchScope = {};
            repo.repoSearchSelected.reset();
            repo.repoSearchExpanded.reset();
            repo.repoSearchExcerptCache.clear();
            repo.repoSearchBatches.reset();

            auto* detailCache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
            if (detailCache) {
                detailCache->patch.key.commit.clear();
                detailCache->commitDetailDiff.clear();
                detailCache->commitDetailBody.clear();
                detailCache->commitDetailAuthorEmail.clear();
                detailCache->commitDetailParents.clear();
            }

            auto* branchDialog = ecs::find_singleton<ecs::BranchDialogState, ecs::ActiveTab>();
            if (branchDialog) {
                branchDialog->newBranchName.clear();
                branchDialog->deleteBranchName.clear();
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

            repo.request_refresh();
            (void)repo.begin_refresh();

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

            auto logResult = git::git_log(repoPath, static_cast<int>(git::kCommitLogPageSize), 0);
            if (logResult.success()) {
                repo.commitLog = git::parse_log(logResult.stdout_str());
                repo.commitLogPage.requestedPageSize = git::kCommitLogPageSize;
                repo.commitLogPage.finish(repo.commitLog.size());
            }

            auto diffResult = git::git_diff(repoPath);
            auto stagedResult = git::git_run(repoPath, {"diff", "--cached"});
            repo.stagedDiff = stagedResult.success() ? git::parse_diff(stagedResult.stdout_str()) : std::vector<ecs::FileDiff>{};
            repo.untrackedReview = {};
            ++repo.dataGeneration;
            auto filesResult = git::git_run(repoPath, {"ls-files", "--cached", "--others", "--exclude-standard", "-z"});
            repo.allFilePaths = git::parse_null_paths(filesResult.stdout_str());
            ++repo.allFilePathsGeneration;
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

            repo.statusKnown = true;
            repo.branchKnown = true;
            repo.settle_refresh();
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

        if (cmd.is("set_open_path")) {
            // Feeds the Open Repository consumer (TabBarSystem) in test
            // mode: the path Cmd+O / File > Open will open, standing in for
            // the native folder panel.
            if (!cmd.has_args(1)) {
                cmd.fail("set_open_path requires a path argument");
                return;
            }
            setenv("FH_TEST_OPEN_PATH", cmd.arg(0).c_str(), 1);
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
        // Optional line count: append that many numbered lines, enough to
        // make a diff several screens tall.
        // "text LINE" appends that line verbatim instead.
        const bool text = cmd.has_args(3) && cmd.args[1] == "text";
        const int lines = cmd.has_args(2) && !text ? std::max(1, std::atoi(cmd.args[1].c_str())) : 0;
        if (text) {
            ofs << e2e_joined_arg(cmd.args, 2) << "\n";
        }
        else if (lines == 0) ofs << "# edited by e2e test\n";
        for (int i = 1; i <= lines; ++i) ofs << "e2e_line_" << i << "\n";
        ofs.close();

        log_info("touch_file: wrote to {}", filePath.string());
        cmd.consume();
    }
};

// ACP sidebar agent drivers: acp_start [agent-id], acp_prompt <text>,
// acp_answer <option-id>, acp_stop. State is asserted through the
// acp_state / acp_permission / acp_transcript properties (validate).
struct HandleAcpCommands : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || (!cmd.is("acp_start") && !cmd.is("acp_prompt") &&
                                  !cmd.is("acp_answer") && !cmd.is("acp_stop"))) return;
        auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        auto* agent = ecs::find_singleton<ecs::AgentComponent, ecs::ActiveTab>();
        if (!repo || !agent) { cmd.fail("acp: no active repo/agent"); return; }
        if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) {
            layout->sidebarMode = ecs::LayoutComponent::SidebarMode::Agent;
            layout->sidebarVisible = true;
        }
        if (cmd.is("acp_start")) {
            const std::string id = cmd.has_args(1) ? cmd.args[0] : agent->agentId;
            if (!ecs::agent_panel::start(*repo, *agent, id)) {
                cmd.fail("acp_start failed: " + agent->lastError);
                return;
            }
        } else if (cmd.is("acp_prompt")) {
            if (!cmd.has_args(1)) { cmd.fail("acp_prompt requires text"); return; }
            if (!ecs::agent_panel::send_prompt(*repo, *agent, e2e_joined_arg(cmd.args, 0))) {
                cmd.fail("acp_prompt rejected (state " + std::string(acp::phase_name(agent->phase())) + ")");
                return;
            }
        } else if (cmd.is("acp_answer")) {
            if (!agent->permission || !agent->client) { cmd.fail("acp_answer: no permission pending"); return; }
            // Arg is an optionId, or an option kind (allow_once, ...) —
            // kinds are stable across agents, ids are not.
            std::string choice = cmd.has_args(1) ? cmd.args[0] : "";
            if (!choice.empty()) {
                bool isId = false;
                for (const auto& option : agent->permission->options) isId |= option.id == choice;
                if (!isId)
                    for (const auto& option : agent->permission->options)
                        if (option.kind == choice) { choice = option.id; break; }
            }
            agent->client->answer_permission(agent->permission->id, choice);
            agent->permission.reset();
        } else if (cmd.is("acp_stop")) {
            ecs::agent_panel::stop(*agent);
        }
        cmd.consume();
    }
};
