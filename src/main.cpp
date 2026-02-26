#include <argh.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#ifdef __APPLE__
#include <copyfile.h>
extern "C" void metal_activate_app(void);
extern "C" void metal_wait_all_screenshots(void);
#endif

#include <afterhours/src/logging.h>
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "ui_context.h"
#include <afterhours/src/plugins/ui/validation_systems.h>
#include "util/process.h"

#include "../vendor/afterhours/src/ecs.h"

#include "ecs/components.h"
#include "ecs/app_reset.h"
#include "ecs/async_git_refresh_system.h"
#include "ecs/file_watcher_system.h"
#include "ecs/layout_system.h"
#include "ecs/menu_bar_system.h"
#include "ecs/sidebar_system.h"
#include "ecs/status_bar_system.h"
#include "ecs/tab_bar_system.h"
#include "ecs/toolbar_system.h"
#include "ecs/validation_summary_system.h"
#include "git/git_runner.h"
#include "git/git_parser.h"

// E2E testing support
#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>

// Main render system - begin_drawing/clear_background done in app_frame
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

// Consumes 'resize' commands as no-ops (used with --e2e-no-resize)
struct SkipResizeCommand : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("resize")) return;
        cmd.consume();
    }
};

// Custom E2E command: make_test_repo
// Resets the test repo from a cached template using fast filesystem operations.
// Falls back to the shell script for first-time template creation.
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

        // Reset UI state before loading new repo
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

        // Switch the active tab to the test repo and load data synchronously
        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<ecs::RepoComponent>()
                                .whereHasComponent<ecs::ActiveTab>()
                                .gen();
        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<ecs::RepoComponent>();
            log_info("make_test_repo: switching from '{}' to '{}'", repo.repoPath, repoPath);
            repo.repoPath = repoPath;
            repo.selectedFilePath.clear();
            repo.selectedCommitHash.clear();
            repo.cachedCommitHash.clear();
            repo.commitDetailDiff.clear();
            repo.commitDetailBody.clear();
            repo.commitDetailAuthorEmail.clear();
            repo.commitDetailParents.clear();
            repo.showNewBranchDialog = false;
            repo.newBranchName.clear();
            repo.showDeleteBranchDialog = false;
            repo.deleteBranchName.clear();
            repo.showForceDeleteDialog = false;

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

            // Synchronous refresh — data is ready this frame, no waits needed
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

// Custom E2E command: reset_ui
// Restores UI to default state: sidebar visible, command log hidden,
// default view modes, close all extra tabs.
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

// Custom E2E command: new_tab / close_tab
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

// touch_file <filename> — appends a line to a file in the active repo's working tree.
// Used by E2E tests to simulate external edits and verify the file watcher picks them up.
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

// Flag for wait_for_refresh gating (set by system, read by app_frame).
// Declared here so it's visible to both HandleWaitForRefresh and app_frame.
namespace e2e_refresh_gate {
inline bool triggered = false;
}

// HandleWaitForRefresh: consumes the "wait_for_refresh" E2E command immediately
// and sets a flag that gates the runner in app_frame() until the async refresh
// completes. This avoids the runner advancing to subsequent commands too early.
struct HandleWaitForRefresh : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("wait_for_refresh")) return;
        cmd.consume();
        e2e_refresh_gate::triggered = true;
    }
};

// Shared state between main() and the run() callbacks
namespace app_state {

afterhours::SystemManager* systemManager = nullptr;
afterhours::Entity* editorEntity = nullptr;
ecs::FileWatcherSystem* fileWatcher = nullptr;

std::string repoPath;

std::chrono::high_resolution_clock::time_point startTime;

// E2E test mode
bool testModeEnabled = false;
bool e2eNoResize = false;
std::string testScriptPath;
std::string testScriptDir;
std::string screenshotDir = "output/screenshots";
float e2eTimeout = 30.0f;
afterhours::testing::E2ERunner e2eRunner;
bool waitingForRefresh = false;
float refreshWaitElapsed = 0.0f;
std::string pendingScreenshotName;

// Validation
std::string validationReportPath;

}  // namespace app_state

struct HandleFileWatcherToggle : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed()) return;
        if (cmd.is("enable_file_watcher")) {
            if (app_state::fileWatcher) app_state::fileWatcher->disabled = false;
            cmd.consume();
        } else if (cmd.is("disable_file_watcher")) {
            if (app_state::fileWatcher) app_state::fileWatcher->disabled = true;
            cmd.consume();
        }
    }
};

// Init callback: runs after Sokol/Metal window is created
static void app_init() {
    using namespace afterhours;
    auto t0 = std::chrono::high_resolution_clock::now();

    {
        Preload::get().init("floatinghotel").make_singleton();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    log_info("  Preload+fonts: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    {
        Settings::get().auto_save_enabled = false;
        Settings::get().load_save_file();
    }

    {
        ui_imm::initUIContext(Settings::get().get_window_width(),
                              Settings::get().get_window_height());

        auto& styling = afterhours::ui::imm::UIStylingDefaults::get();
        styling.set_default_font(
            afterhours::ui::UIComponent::DEFAULT_FONT,
            afterhours::ui::pixels(14.0f));

        // Use Adaptive scaling: pixels() = logical pixels * ui_scale (web-like).
        // Font sizes and layout behave like CSS px values.
        styling.set_scaling_mode(afterhours::ui::ScalingMode::Adaptive);

        // Enable UI validation checks (min font size, contrast, etc.)
        // Use Silent mode so per-frame log spam doesn't peg the CPU;
        // ValidationSummarySystem prints a deduplicated report instead.
        styling.enable_development_validation();
        styling.validation.mode = afterhours::ui::ValidationMode::Silent;
        styling.validation.min_font_size = 11.0f;
        if (app_state::testModeEnabled) {
            styling.validation.highlight_violations = false;
        }
    }

    // Create the shared editor entity (layout, menu, command log, settings)
    auto& entity = EntityHelper::createEntity();
    app_state::editorEntity = &entity;

    auto& layoutComp = entity.addComponent<ecs::LayoutComponent>();
    (void)layoutComp;

    auto& menuComp = entity.addComponent<ecs::MenuComponent>();
    (void)menuComp;

    auto& cmdLog = entity.addComponent<ecs::CommandLogComponent>();

    // Create the tab strip singleton
    auto& tabStripEntity = EntityHelper::createEntity();
    auto& tabStrip = tabStripEntity.addComponent<ecs::TabStripComponent>();

    // Helper: create a tab entity for a given repo path
    auto savedPolicy = Settings::get().get_unstaged_policy();
    auto createTab = [&tabStrip, &savedPolicy](const std::string& path, bool makeActive) -> afterhours::Entity& {
        auto& tab = EntityHelper::createEntity();
        tab.addComponent<ecs::Tab>();
        if (makeActive) tab.addComponent<ecs::ActiveTab>();

        auto& repo = tab.addComponent<ecs::RepoComponent>();
        repo.repoPath = path;
        if (!path.empty()) {
            repo.refreshRequested = true;
            Settings::get().add_recent_repo(path);
            std::filesystem::path p(path);
            tab.get<ecs::Tab>().label = p.filename().string();
        }

        auto& editor = tab.addComponent<ecs::CommitEditorComponent>();
        if (savedPolicy == "stage_all") {
            editor.unstagedPolicy = ecs::CommitEditorComponent::UnstagedPolicy::StageAll;
        } else if (savedPolicy == "staged_only") {
            editor.unstagedPolicy = ecs::CommitEditorComponent::UnstagedPolicy::CommitStagedOnly;
        }

        tabStrip.tabOrder.push_back(tab.id);
        return tab;
    };

    if (!app_state::repoPath.empty()) {
        // CLI repo specified: single tab
        createTab(app_state::repoPath, true);
    } else {
        // Restore tabs from last session
        auto savedRepos = Settings::get().get_open_repos();
        auto lastActive = Settings::get().get_last_active_repo();

        if (!savedRepos.empty()) {
            bool anyActive = false;
            for (auto& path : savedRepos) {
                bool isActive = (path == lastActive);
                createTab(path, isActive);
                if (isActive) anyActive = true;
            }
            // If last_active_repo didn't match any saved tab, activate the first
            if (!anyActive) {
                auto firstOpt = EntityHelper::getEntityForID(tabStrip.tabOrder[0]);
                if (firstOpt.valid()) firstOpt.asE().addComponent<ecs::ActiveTab>();
            }
        } else if (!lastActive.empty()) {
            // No saved tabs but have a last-active repo (legacy settings)
            createTab(lastActive, true);
        } else {
            // Fresh start: empty welcome tab
            createTab("", true);
        }
    }

    // Wire git log callback to record all git commands in the CommandLogComponent
    git::set_log_callback([&cmdLog](const std::string& cmd,
                                     const std::string& out,
                                     const std::string& err,
                                     bool success) {
        auto now = std::chrono::system_clock::now();
        double ts = static_cast<double>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
        cmdLog.entries.push_back({cmd, out, err, success, ts});
    });

    // Setup SystemManager with all systems
    static SystemManager sm;
    app_state::systemManager = &sm;

    {
        // Ensure toast and modal singletons exist before any UI system
        // accesses them (e.g. sidebar renders modal dialogs)
        afterhours::toast::enforce_singletons(sm);
        afterhours::modal::enforce_singletons(sm);

        // Pre-layout (context begin, clear children)
        ui_imm::registerUIPreLayoutSystems(sm);

        // Tab sync: capture view mode changes into active Tab each frame
        sm.register_update_system(std::make_unique<ecs::TabSyncSystem>());

        // Layout calculation must run before UI systems so panel rects
        // (toolbar, sidebar, status bar, etc.) have correct sizes when
        // the UI-creating systems read them.
        sm.register_update_system(std::make_unique<ecs::LayoutUpdateSystem>());

        // UI-creating systems (order determines visual stacking;
        // later systems draw on top of earlier ones)
        sm.register_update_system(std::make_unique<ecs::TabBarSystem>());
        sm.register_update_system(std::make_unique<ecs::ToolbarSystem>());
        sm.register_update_system(std::make_unique<ecs::SidebarSystem>());
        sm.register_update_system(std::make_unique<ecs::MainContentSystem>());
        sm.register_update_system(std::make_unique<ecs::StatusBarSystem>());
        // MenuBarSystem runs last so dropdown elements draw on top of
        // toolbar/sidebar when a menu is open
        sm.register_update_system(std::make_unique<ecs::MenuBarSystem>());

        // Post-layout (entity mapping, autolayout, interactions)
        ui_imm::registerUIPostLayoutSystems(sm);

        // Update systems
        auto fileWatcherPtr = std::make_unique<ecs::FileWatcherSystem>();
        app_state::fileWatcher = fileWatcherPtr.get();
        if (app_state::testModeEnabled) {
            fileWatcherPtr->disabled = true;
        }
        sm.register_update_system(std::move(fileWatcherPtr));
        sm.register_update_system(std::make_unique<ecs::AsyncGitDataRefreshSystem>());

        // Toast notification systems
        ui_imm::registerToastSystems(sm);

        // Modal dialog systems
        ui_imm::registerModalSystems(sm);

        // E2E testing systems (only in test mode)
        if (app_state::testModeEnabled) {
            if (app_state::e2eNoResize) {
                sm.register_update_system(std::make_unique<SkipResizeCommand>());
            }
            sm.register_update_system(std::make_unique<HandleMakeTestRepo>());
            sm.register_update_system(std::make_unique<HandleResetUI>());
            sm.register_update_system(std::make_unique<HandleTabCommands>());
            sm.register_update_system(std::make_unique<HandleTouchFile>());
            sm.register_update_system(std::make_unique<HandleWaitForRefresh>());
            sm.register_update_system(std::make_unique<HandleFileWatcherToggle>());
            afterhours::testing::register_builtin_handlers(sm);
            sm.register_update_system(
                std::make_unique<afterhours::testing::HandleScreenshotCommand>(
                    [](const std::string& name) {
                        std::filesystem::path dir =
                            std::filesystem::absolute(app_state::screenshotDir);
                        std::filesystem::create_directories(dir);
                        std::filesystem::path path = dir / (name + ".png");
                        afterhours::graphics::take_screenshot(path.c_str());
                        log_info("Screenshot: {}", path.string());
                    }));
            afterhours::testing::ui_commands::register_ui_commands<InputAction>(sm);
            afterhours::testing::register_unknown_handler(sm);
            afterhours::testing::register_cleanup(sm);
        }

        // Render systems
        sm.register_render_system(
            std::make_unique<MainRenderSystem>());
        ui_imm::registerUIRenderSystems(sm);
        ui_imm::registerModalRenderSystems(sm);

        // UI validation systems (design rule enforcement)
        afterhours::ui::validation::register_systems<InputAction>(sm);

        {
            auto summary = std::make_unique<ecs::ValidationSummarySystem>();
            summary->settle_frames = 5;
            if (!app_state::validationReportPath.empty()) {
                summary->report_path = app_state::validationReportPath;
            }
            auto* summaryPtr = summary.get();
            sm.register_update_system(std::move(summary));

            auto trigger = std::make_unique<ecs::ValidationSummaryTrigger>();
            trigger->summary = summaryPtr;
            sm.register_update_system(std::move(trigger));
        }
    }

    // Single settings write for all init-time mutations, then re-enable auto-save
    Settings::get().write_save_file();
    Settings::get().auto_save_enabled = true;

    auto t2 = std::chrono::high_resolution_clock::now();
    log_info("  Systems registration: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

    // In test mode, bring the window to the foreground so the Metal display
    // link runs at full speed instead of being throttled to ~0.5 FPS.
#ifdef __APPLE__
    if (app_state::testModeEnabled) {
        metal_activate_app();
    }
#endif

    // Measure startup time
    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            readyTime - app_state::startTime)
            .count();
    log_info("Startup time: {} ms (from graphics::run to app_init done)", startupMs);
}

// Process E2E commands in a tight loop without rendering, breaking when
// a screenshot is needed or when we must wait for async operations.
static void e2e_tick_loop(float real_dt) {
    constexpr int MAX_TICKS = 200;
    constexpr float SIM_DT = 1.0f / 60.0f;

    for (int i = 0; i < MAX_TICKS; ++i) {
        afterhours::testing::test_input::reset_frame();

        if (e2e_refresh_gate::triggered) {
            e2e_refresh_gate::triggered = false;
            app_state::waitingForRefresh = true;
            app_state::refreshWaitElapsed = 0.0f;
        }

        if (app_state::waitingForRefresh) {
            app_state::refreshWaitElapsed += real_dt;
            constexpr float MAX_REFRESH_WAIT = 5.0f;
            bool refreshDone = true;
            auto repoQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>()
                .whereHasComponent<ecs::ActiveTab>()
                .gen();
            if (!repoQ.empty()) {
                auto& repo = repoQ[0].get().get<ecs::RepoComponent>();
                refreshDone = !repo.refreshRequested && !repo.isRefreshing;
            }
            if (refreshDone || app_state::refreshWaitElapsed > MAX_REFRESH_WAIT) {
                app_state::waitingForRefresh = false;
                continue;
            }
            break;
        }

        app_state::e2eRunner.tick(SIM_DT);

        if (!app_state::pendingScreenshotName.empty()) break;
        if (app_state::e2eRunner.is_finished()) break;

        auto& entities = afterhours::EntityHelper::get_entities_for_mod();
        app_state::systemManager->tick_all(entities, SIM_DT);
        afterhours::EntityHelper::cleanup();

        // A surviving pending command (e.g. expect_text retrying) needs a
        // render pass to update VisibleTextRegistry before it can succeed.
        if (!afterhours::EntityQuery()
                .whereHasComponent<afterhours::testing::PendingE2ECommand>()
                .gen().empty()) break;
    }
}

// Render one frame and take any pending screenshot.
static void e2e_render_and_screenshot(float dt) {
    afterhours::testing::test_input::reset_frame();
    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(afterhours::Color{30, 30, 30, 255});
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();

    if (!app_state::pendingScreenshotName.empty()) {
        std::filesystem::path dir =
            std::filesystem::absolute(app_state::screenshotDir);
        std::filesystem::create_directories(dir);
        std::filesystem::path path = dir / (app_state::pendingScreenshotName + ".png");
        afterhours::graphics::take_screenshot(path.c_str());
        app_state::pendingScreenshotName.clear();
    }
}

// Frame callback: runs every frame
static void app_frame() {
    float dt = afterhours::graphics::get_frame_time();

    if (app_state::testModeEnabled && app_state::e2eRunner.has_commands()) {
        e2e_tick_loop(dt);
        e2e_render_and_screenshot(dt);

        if (app_state::e2eRunner.is_finished()) {
#ifdef __APPLE__
            metal_wait_all_screenshots();
#endif
            app_state::e2eRunner.print_results();
            // In test mode, exit immediately to avoid waiting for the display
            // link to fire another frame for the quit sequence.
            _exit(app_state::e2eRunner.has_failed() ? 1 : 0);
        }
        return;
    }

    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(
        afterhours::Color{30, 30, 30, 255});
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();
}

// Cleanup callback: runs when window is closing
static void app_cleanup() {
    // Batch all cleanup mutations into a single disk write
    Settings::get().auto_save_enabled = false;

    auto tabStripQ = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<ecs::TabStripComponent>().gen();
    if (!tabStripQ.empty()) {
        auto& tabStrip = tabStripQ[0].get().get<ecs::TabStripComponent>();
        std::vector<std::string> openRepos;
        std::string activeRepo;
        for (auto tabId : tabStrip.tabOrder) {
            auto opt = afterhours::EntityHelper::getEntityForID(tabId);
            if (!opt.valid() || !opt->has<ecs::RepoComponent>()) continue;
            auto& repo = opt->get<ecs::RepoComponent>();
            if (!repo.repoPath.empty()) {
                openRepos.push_back(repo.repoPath);
                if (opt->has<ecs::ActiveTab>()) {
                    activeRepo = repo.repoPath;
                }
            }
        }
        Settings::get().set_open_repos(openRepos);
        if (!activeRepo.empty()) {
            Settings::get().set_last_active_repo(activeRepo);
        }
    }

    Settings::get().write_save_file();
}

int main(int argc, char* argv[]) {
    auto mainStart = std::chrono::high_resolution_clock::now();
    argh::parser cmdl(argc, argv);

    // Parse repo path from first positional argument
    std::string repoPath;
    cmdl(1, "") >> repoPath;

    // Parse test mode flags
    app_state::testModeEnabled = cmdl["--test-mode"];
    app_state::e2eNoResize = cmdl["--e2e-no-resize"];
    for (auto& [name, value] : cmdl.params()) {
        if (name == "screenshot-dir") {
            app_state::screenshotDir = value;
        } else if (name == "test-script") {
            app_state::testScriptPath = value;
        } else if (name == "test-script-dir" || name == "test-dir") {
            app_state::testScriptDir = value;
        } else if (name == "e2e-timeout") {
            app_state::e2eTimeout = std::stof(value);
        } else if (name == "validation-report") {
            app_state::validationReportPath = value;
        }
    }

    // If test script specified, enable test mode
    if (!app_state::testScriptPath.empty() || !app_state::testScriptDir.empty()) {
        app_state::testModeEnabled = true;
        afterhours::testing::test_input::detail::test_mode = true;
    }

    // Load E2E scripts
    if (!app_state::testScriptDir.empty()) {
        app_state::e2eRunner.load_scripts_from_directory(app_state::testScriptDir);
    } else if (!app_state::testScriptPath.empty()) {
        app_state::e2eRunner.load_script(app_state::testScriptPath);
    }
    app_state::e2eRunner.set_timeout(app_state::e2eTimeout);
    app_state::e2eRunner.set_reset_callback([] {
        auto layoutQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::LayoutComponent>().gen();
        if (!layoutQ.empty()) {
            ecs::reset_layout_defaults(layoutQ[0].get().get<ecs::LayoutComponent>());
        }
        auto editorQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::CommitEditorComponent>()
            .whereHasComponent<ecs::ActiveTab>().gen();
        if (!editorQ.empty()) {
            ecs::reset_commit_editor(editorQ[0].get().get<ecs::CommitEditorComponent>());
        }
        auto menuQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::MenuComponent>().gen();
        if (!menuQ.empty()) {
            ecs::reset_menus(menuQ[0].get().get<ecs::MenuComponent>());
        }
    });
    app_state::e2eRunner.set_property_getter([](const std::string& key) -> std::string {
        auto layoutQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::LayoutComponent>().gen();
        auto repoQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::RepoComponent>()
            .whereHasComponent<ecs::ActiveTab>().gen();
        auto editorQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::CommitEditorComponent>()
            .whereHasComponent<ecs::ActiveTab>().gen();

        if (key == "sidebar_visible") {
            if (!layoutQ.empty())
                return layoutQ[0].get().get<ecs::LayoutComponent>().sidebarVisible ? "true" : "false";
        } else if (key == "command_log_visible") {
            if (!layoutQ.empty())
                return layoutQ[0].get().get<ecs::LayoutComponent>().commandLogVisible ? "true" : "false";
        } else if (key == "diff_view_mode") {
            if (!layoutQ.empty()) {
                auto m = layoutQ[0].get().get<ecs::LayoutComponent>().diffViewMode;
                return m == ecs::LayoutComponent::DiffViewMode::Inline ? "Inline" : "SideBySide";
            }
        } else if (key == "file_view_mode") {
            if (!layoutQ.empty()) {
                switch (layoutQ[0].get().get<ecs::LayoutComponent>().fileViewMode) {
                    case ecs::LayoutComponent::FileViewMode::Flat: return "Flat";
                    case ecs::LayoutComponent::FileViewMode::Tree: return "Tree";
                    case ecs::LayoutComponent::FileViewMode::All: return "All";
                    default: return "Unknown";
                }
            }
        } else if (key == "sidebar_mode") {
            if (!layoutQ.empty()) {
                auto m = layoutQ[0].get().get<ecs::LayoutComponent>().sidebarMode;
                return m == ecs::LayoutComponent::SidebarMode::Changes ? "Changes" : "Refs";
            }
        } else if (key == "staged_count") {
            if (!repoQ.empty())
                return std::to_string(repoQ[0].get().get<ecs::RepoComponent>().stagedFiles.size());
        } else if (key == "unstaged_count") {
            if (!repoQ.empty()) {
                auto& r = repoQ[0].get().get<ecs::RepoComponent>();
                return std::to_string(r.unstagedFiles.size());
            }
        } else if (key == "untracked_count") {
            if (!repoQ.empty())
                return std::to_string(repoQ[0].get().get<ecs::RepoComponent>().untrackedFiles.size());
        } else if (key == "branch") {
            if (!repoQ.empty())
                return repoQ[0].get().get<ecs::RepoComponent>().currentBranch;
        } else if (key == "selected_file") {
            if (!repoQ.empty())
                return repoQ[0].get().get<ecs::RepoComponent>().selectedFilePath;
        } else if (key == "is_amend") {
            if (!editorQ.empty())
                return editorQ[0].get().get<ecs::CommitEditorComponent>().isAmend ? "true" : "false";
        } else if (key == "refresh_requested") {
            if (!repoQ.empty())
                return repoQ[0].get().get<ecs::RepoComponent>().refreshRequested ? "true" : "false";
        } else if (key == "tab_count") {
            auto tabQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::TabStripComponent>().gen();
            if (!tabQ.empty())
                return std::to_string(tabQ[0].get().get<ecs::TabStripComponent>().tabOrder.size());
            return "0";
        } else if (key == "active_tab_label") {
            auto tabQ = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::Tab>()
                .whereHasComponent<ecs::ActiveTab>().gen();
            if (!tabQ.empty())
                return tabQ[0].get().get<ecs::Tab>().label;
        }
        return "";
    });
    app_state::e2eRunner.set_screenshot_callback([](const std::string& name) {
        app_state::pendingScreenshotName = name;
    });

    // Resolve relative paths to absolute
    if (!repoPath.empty()) {
        repoPath = std::filesystem::absolute(repoPath).string();
    }

    // Quick validation: check the directory exists and contains .git
    // (avoids spawning a git subprocess on startup)
    if (!repoPath.empty()) {
        if (!std::filesystem::is_directory(repoPath)) {
            fprintf(stderr, "Error: '%s' is not a directory\n",
                    repoPath.c_str());
            return 1;
        }
        auto gitDir = std::filesystem::path(repoPath) / ".git";
        if (!std::filesystem::exists(gitDir)) {
            fprintf(stderr, "Error: '%s' is not a git repository\n",
                    repoPath.c_str());
            return 1;
        }
    }

    app_state::repoPath = repoPath;

    {
        auto preGfxMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - mainStart).count();
        log_info("Pre-graphics init: {} ms", preGfxMs);
    }

    app_state::startTime = std::chrono::high_resolution_clock::now();

    afterhours::graphics::RunConfig cfg;
    cfg.width = 1200;
    cfg.height = 800;
    cfg.title = "floatinghotel";
    cfg.target_fps = 200;
    cfg.flags = afterhours::graphics::FLAG_WINDOW_RESIZABLE;
    cfg.init = app_init;
    cfg.frame = app_frame;
    cfg.cleanup = app_cleanup;

    afterhours::graphics::run(cfg);

    return 0;
}
