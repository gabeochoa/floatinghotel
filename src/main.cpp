#include <argh.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "logging.h"
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
#include "ecs/toolbar_system.h"
#include "git/git_runner.h"

// E2E testing support
#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>

// Main render system - begin_drawing/clear_background done in app_frame
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

// Shared state between main() and the run() callbacks
namespace app_state {

afterhours::SystemManager* systemManager = nullptr;
afterhours::Entity* editorEntity = nullptr;

std::string repoPath;

std::chrono::high_resolution_clock::time_point startTime;

// E2E test mode
bool testModeEnabled = false;
std::string testScriptPath;
std::string testScriptDir;
std::string screenshotDir = "output/screenshots";
float e2eTimeout = 30.0f;
afterhours::testing::E2ERunner e2eRunner;

}  // namespace app_state

// Init callback: runs after Sokol/Metal window is created
static void app_init() {
    using namespace afterhours;

    {
        SCOPED_TIMER("Preload and singletons");
        Preload::get().init("floatinghotel").make_singleton();
    }

    {
        SCOPED_TIMER("Load settings");
        Settings::get().load_save_file();
    }

    {
        SCOPED_TIMER("UI context init");
        ui_imm::initUIContext(Settings::get().get_window_width(),
                              Settings::get().get_window_height());

        afterhours::ui::imm::UIStylingDefaults::get().set_default_font(
            afterhours::ui::UIComponent::DEFAULT_FONT,
            afterhours::ui::pixels(16.0f));

        // Enable UI validation in development mode (min font size, contrast,
        // resolution independence, etc.)
        afterhours::ui::imm::UIStylingDefaults::get()
            .enable_development_validation();
    }

    // Create the editor entity with layout + repo components
    auto& entity = EntityHelper::createEntity();
    app_state::editorEntity = &entity;

    auto& layoutComp = entity.addComponent<ecs::LayoutComponent>();
    (void)layoutComp;  // Uses defaults from component definition

    auto& repoComp = entity.addComponent<ecs::RepoComponent>();
    repoComp.repoPath = app_state::repoPath;
    if (!repoComp.repoPath.empty()) {
        repoComp.refreshRequested = true;
    }

    auto& menuComp = entity.addComponent<ecs::MenuComponent>();
    (void)menuComp;  // Menu bar state

    auto& commitEditor = entity.addComponent<ecs::CommitEditorComponent>();
    // Load remembered unstaged policy from settings
    {
        auto savedPolicy = Settings::get().get_unstaged_policy();
        if (savedPolicy == "stage_all") {
            commitEditor.unstagedPolicy =
                ecs::CommitEditorComponent::UnstagedPolicy::StageAll;
        } else if (savedPolicy == "staged_only") {
            commitEditor.unstagedPolicy =
                ecs::CommitEditorComponent::UnstagedPolicy::CommitStagedOnly;
        }
        // Default is Ask
    }

    auto& cmdLog = entity.addComponent<ecs::CommandLogComponent>();

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
        SCOPED_TIMER("Register all systems");

        // Ensure toast and modal singletons exist before any UI system
        // accesses them (e.g. sidebar renders modal dialogs)
        afterhours::toast::enforce_singletons(sm);
        afterhours::modal::enforce_singletons(sm);

        // Pre-layout (context begin, clear children)
        ui_imm::registerUIPreLayoutSystems(sm);

        // Layout calculation must run before UI systems so panel rects
        // (toolbar, sidebar, status bar, etc.) have correct sizes when
        // the UI-creating systems read them.
        sm.register_update_system(std::make_unique<ecs::LayoutUpdateSystem>());

        // UI-creating systems (order determines visual stacking)
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
            afterhours::testing::register_builtin_handlers(sm);
            sm.register_update_system(
                std::make_unique<afterhours::testing::HandleScreenshotCommand>(
                    [](const std::string& name) {
                        std::filesystem::path dir =
                            std::filesystem::absolute(app_state::screenshotDir);
                        std::filesystem::create_directories(dir);
                        std::filesystem::path path = dir / (name + ".png");
                        afterhours::graphics::take_screenshot(path.c_str());
                        LOG_INFO("Screenshot: %s", path.c_str());
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
    }

    // Measure startup time
    auto readyTime = std::chrono::high_resolution_clock::now();
    auto startupMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            readyTime - app_state::startTime)
            .count();
    LOG_INFO("Startup time: %lld ms", static_cast<long long>(startupMs));
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
    argh::parser cmdl(argc, argv);

    // Parse repo path from first positional argument
    std::string repoPath;
    cmdl(1, "") >> repoPath;

    // Parse test mode flags
    app_state::testModeEnabled = cmdl["--test-mode"];
    for (auto& [name, value] : cmdl.params()) {
        if (name == "screenshot-dir") {
            app_state::screenshotDir = value;
        } else if (name == "test-script") {
            app_state::testScriptPath = value;
        } else if (name == "test-script-dir") {
            app_state::testScriptDir = value;
        } else if (name == "e2e-timeout") {
            app_state::e2eTimeout = std::stof(value);
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
    app_state::e2eRunner.set_screenshot_callback([](const std::string& name) {
        std::filesystem::path dir =
            std::filesystem::absolute(app_state::screenshotDir);
        std::filesystem::create_directories(dir);
        std::filesystem::path path = dir / (name + ".png");
        afterhours::graphics::take_screenshot(path.c_str());
        LOG_INFO("Screenshot: %s", path.c_str());
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
