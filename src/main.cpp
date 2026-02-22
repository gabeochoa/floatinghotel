#include <argh.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <afterhours/src/logging.h>
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "ui_context.h"
#include <afterhours/src/plugins/ui/validation_systems.h>
#include "util/process.h"

#include "../vendor/afterhours/src/ecs.h"

#include "ecs/components.h"
#include "ecs/async_git_refresh_system.h"
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
// Runs scripts/setup_test_repo.sh, switches the app to the new repo,
// and does a SYNCHRONOUS git refresh so data is ready immediately.
struct HandleMakeTestRepo : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("make_test_repo")) return;

        // Find the setup script relative to the executable's working directory
        std::string script = "scripts/setup_test_repo.sh";
        auto result = run_process("", {"bash", script});
        if (!result.success()) {
            cmd.fail("make_test_repo: script failed: " + result.stderr_str);
            return;
        }

        // Script prints the repo path on stdout
        std::string repoPath = result.stdout_str;
        // Trim trailing whitespace/newlines
        while (!repoPath.empty() && (repoPath.back() == '\n' || repoPath.back() == '\r' || repoPath.back() == ' '))
            repoPath.pop_back();

        if (repoPath.empty()) {
            cmd.fail("make_test_repo: script returned empty path");
            return;
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
        } else {
            log_warn("make_test_repo: no RepoComponent entity found!");
        }

        log_info("make_test_repo: done, path={}", repoPath);
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
    }
};

// Shared state between main() and the run() callbacks
namespace app_state {

afterhours::SystemManager* systemManager = nullptr;
afterhours::Entity* editorEntity = nullptr;

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

// Validation
std::string validationReportPath;

}  // namespace app_state

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

        // Enable UI validation in development mode (min font size, contrast,
        // resolution independence, etc.)
        styling.enable_development_validation();
        // Lower min font threshold for 720p (11px is legible at this res)
        styling.validation.min_font_size = 11.0f;
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

    // Create the default tab entity (owns repo + commit editor + tab state)
    auto& tabEntity = EntityHelper::createEntity();
    tabEntity.addComponent<ecs::Tab>();
    tabEntity.addComponent<ecs::ActiveTab>();

    auto& repoComp = tabEntity.addComponent<ecs::RepoComponent>();
    repoComp.repoPath = app_state::repoPath;
    if (!repoComp.repoPath.empty()) {
        repoComp.refreshRequested = true;
        Settings::get().add_recent_repo(repoComp.repoPath);
    }

    auto& commitEditor = tabEntity.addComponent<ecs::CommitEditorComponent>();
    {
        auto savedPolicy = Settings::get().get_unstaged_policy();
        if (savedPolicy == "stage_all") {
            commitEditor.unstagedPolicy =
                ecs::CommitEditorComponent::UnstagedPolicy::StageAll;
        } else if (savedPolicy == "staged_only") {
            commitEditor.unstagedPolicy =
                ecs::CommitEditorComponent::UnstagedPolicy::CommitStagedOnly;
        }
    }

    // Update tab label from repo path and record in recent repos
    if (!repoComp.repoPath.empty()) {
        std::filesystem::path p(repoComp.repoPath);
        tabEntity.get<ecs::Tab>().label = p.filename().string();
        Settings::get().add_recent_repo(repoComp.repoPath);
    }

    tabStrip.tabOrder.push_back(tabEntity.id);

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

        // UI-creating systems (order determines visual stacking)
        sm.register_update_system(std::make_unique<ecs::TabBarSystem>());
        sm.register_update_system(std::make_unique<ecs::MenuBarSystem>());
        sm.register_update_system(std::make_unique<ecs::ToolbarSystem>());
        sm.register_update_system(std::make_unique<ecs::SidebarSystem>());
        sm.register_update_system(std::make_unique<ecs::MainContentSystem>());
        sm.register_update_system(std::make_unique<ecs::StatusBarSystem>());

        // Post-layout (entity mapping, autolayout, interactions)
        ui_imm::registerUIPostLayoutSystems(sm);

        // Update systems
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
            sm.register_update_system(std::make_unique<HandleTabCommands>());
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

        // Validation summary: deduplicates warnings and prints a clean report
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

    auto t2 = std::chrono::high_resolution_clock::now();
    log_info("  Systems registration: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

    // Measure startup time
    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            readyTime - app_state::startTime)
            .count();
    log_info("Startup time: {} ms (from graphics::run to app_init done)", startupMs);
}

// Frame callback: runs every frame
static void app_frame() {
    float dt = afterhours::graphics::get_frame_time();

    // E2E test runner: tick and check completion
    if (app_state::testModeEnabled && app_state::e2eRunner.has_commands()) {
        afterhours::testing::test_input::reset_frame();
        app_state::e2eRunner.tick(dt);
        if (app_state::e2eRunner.is_finished()) {
            app_state::e2eRunner.print_results();
            afterhours::graphics::request_quit();
        }
    }

    afterhours::graphics::begin_drawing();
    // Dark background #1E1E1E
    afterhours::graphics::clear_background(
        afterhours::Color{30, 30, 30, 255});

    app_state::systemManager->run(dt);

    afterhours::graphics::end_drawing();
}

// Cleanup callback: runs when window is closing
static void app_cleanup() {
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
        } else if (name == "test-script-dir") {
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
            auto& l = layoutQ[0].get().get<ecs::LayoutComponent>();
            l.sidebarVisible = true;
            l.commandLogVisible = false;
            l.diffViewMode = ecs::LayoutComponent::DiffViewMode::Inline;
            l.fileViewMode = ecs::LayoutComponent::FileViewMode::Flat;
            l.sidebarMode = ecs::LayoutComponent::SidebarMode::Changes;
        }
        auto editorQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::CommitEditorComponent>()
            .whereHasComponent<ecs::ActiveTab>().gen();
        if (!editorQ.empty()) {
            auto& e = editorQ[0].get().get<ecs::CommitEditorComponent>();
            e.isAmend = false;
            e.commitRequested = false;
            e.showUnstagedDialog = false;
        }
        auto menuQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<ecs::MenuComponent>().gen();
        if (!menuQ.empty()) {
            menuQ[0].get().get<ecs::MenuComponent>().activeMenuIndex = -1;
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
        std::filesystem::path dir =
            std::filesystem::absolute(app_state::screenshotDir);
        std::filesystem::create_directories(dir);
        std::filesystem::path path = dir / (name + ".png");
        afterhours::graphics::take_screenshot(path.c_str());
        log_info("Screenshot: {}", path.string());
    });

    // Resolve relative paths to absolute
    if (!repoPath.empty()) {
        repoPath = std::filesystem::absolute(repoPath).string();
    }

    // Validate that the path is a git repository
    if (!repoPath.empty()) {
        if (!std::filesystem::is_directory(repoPath)) {
            fprintf(stderr, "Error: '%s' is not a directory\n",
                    repoPath.c_str());
            return 1;
        }
        auto result = run_process(
            "", {"git", "-C", repoPath, "rev-parse", "--git-dir"});
        if (!result.success()) {
            fprintf(stderr, "Error: '%s' is not a git repository\n",
                    repoPath.c_str());
            return 1;
        }
    }

    app_state::repoPath = repoPath;

    // If no path specified, try last-used repo from settings
    if (app_state::repoPath.empty()) {
        Settings::get().load_save_file();
        app_state::repoPath = Settings::get().get_last_active_repo();
    }

    // Update last active repo in settings
    if (!app_state::repoPath.empty()) {
        Settings::get().set_last_active_repo(app_state::repoPath);
    }

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
