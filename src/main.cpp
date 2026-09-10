#include <libproc.h>
#include <sys/time.h>
#include <unistd.h>
#include <argh.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>
#include <optional>
#include <algorithm>

#ifdef __APPLE__
extern "C" void metal_activate_app(void);
extern "C" void metal_draw_first_frame_early(void);
extern "C" void metal_headless_frame(void (*fn)(void));
extern "C" void metal_hide_window(void);
extern "C" void metal_wait_all_screenshots(void);
#endif

#include <afterhours/src/gestures_macos.h>
#include <afterhours/src/logging.h>
#include <afterhours/src/shutdown.h>
#include "preload.h"
#include "rl.h"
#include "settings.h"
#include "review_store.h"
#include "ui_context.h"
#include "ui/context_menu.h"
#include "ui/zoom.h"
#include <afterhours/src/plugins/ui/validation_systems.h>
#include "util/process.h"

#include "../vendor/afterhours/src/ecs.h"

#include "ecs/components.h"
#include "ecs/app_reset.h"
#include "ecs/async_git_refresh_system.h"
#include "ecs/file_watcher_system.h"
#include "ecs/layout_system.h"
#include "ecs/main_content_system.h"
#include "ecs/menu_bar_system.h"
#include "ecs/sidebar_system.h"
#include "ecs/status_bar_system.h"
#include "ecs/tab_bar_system.h"
#include "ecs/toolbar_system.h"
#include "ecs/network_ops_system.h"
#include "ecs/validation_summary_system.h"
#include "ecs/zoom_system.h"
#include "git/git_runner.h"
#include "git/git_parser.h"

// E2E testing support
#include <afterhours/src/plugins/e2e_testing/e2e_testing.h>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>
#include <afterhours/src/plugins/e2e_testing/perf_commands.h>
#include "ecs/e2e_command_handlers.h"

// Main render system - begin_drawing/clear_background done in app_frame
struct MainRenderSystem : afterhours::System<> {
    void once(float) override {}
};

// Flag for wait_for_refresh gating (set by system, read by app_frame).
// Declared here so it's visible to both HandleWaitForRefresh and app_frame.
namespace e2e_refresh_gate {
inline bool triggered = false;
inline bool file_change_triggered = false;
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

// HandleWaitForFileChange: consumes "wait_for_file_change" and gates the runner
// until the file watcher has fired once more and the refresh it requested has
// finished. `wait N` counts simulated ticks, which the headless loop runs at
// well under a millisecond each, while the watcher's cooldown and FSEvents
// latency are wall-clock seconds; this gate waits on the wall clock instead.
struct HandleWaitForFileChange : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("wait_for_file_change")) return;
        cmd.consume();
        e2e_refresh_gate::file_change_triggered = true;
    }
};

// bench_frames N: render N full frames back to back, outside the E2E tick
// loop, and report wall time per frame plus the per-system profile over
// exactly those frames. Feeds expect_fps_above / expect_p99_below.
namespace e2e_bench {
inline int requested = 0;
inline std::vector<float> samples_ms;
inline std::optional<float> avg_ms() {
    if (samples_ms.empty()) return std::nullopt;
    double sum = 0.0;
    for (float v : samples_ms) sum += v;
    return static_cast<float>(sum / static_cast<double>(samples_ms.size()));
}
inline std::optional<float> percentile_ms(double q) {
    if (samples_ms.empty()) return std::nullopt;
    std::vector<float> sorted = samples_ms;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(q * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}
}  // namespace e2e_bench

struct HandleBenchFrames : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("bench_frames")) return;
        int n = cmd.has_args(1) ? cmd.arg_as<int>(0) : 120;
        e2e_bench::requested = std::max(1, n);
        cmd.consume();
    }
};

// Shared state between main() and the run() callbacks
// Git command logging is invoked from detached git worker threads (git_run in
// git_runner.cpp), but the CommandLogComponent it feeds is read by the UI on the
// main thread. Writing the ECS component directly from a worker races with those
// reads and corrupts the heap (surfaces as a bad Entity pointer during tick).
// Stage entries under a mutex on the worker side, then drain them into the
// component on the main thread once per frame.
static std::mutex g_gitLogMutex;
static std::vector<ecs::CommandLogComponent::Entry> g_pendingGitLog;
static ecs::CommandLogComponent* g_cmdLogSink = nullptr;

static void drain_git_log() {
    if (!g_cmdLogSink) return;
    std::lock_guard<std::mutex> lock(g_gitLogMutex);
    if (g_pendingGitLog.empty()) return;
    auto& dst = g_cmdLogSink->entries;
    dst.insert(dst.end(),
               std::make_move_iterator(g_pendingGitLog.begin()),
               std::make_move_iterator(g_pendingGitLog.end()));
    g_pendingGitLog.clear();
}

namespace app_state {

afterhours::SystemManager* systemManager = nullptr;
afterhours::Entity* editorEntity = nullptr;
ecs::FileWatcherSystem* fileWatcher = nullptr;

std::string repoPath;

std::chrono::high_resolution_clock::time_point startTime;

// E2E test mode
bool testModeEnabled = false;
bool e2eNoResize = false;
bool headless = false;
std::string testScriptPath;
std::string testScriptDir;
std::string screenshotDir = "output/screenshots";
float e2eTimeout = 30.0f;
afterhours::testing::E2ERunner e2eRunner;
bool waitingForRefresh = false;
std::chrono::steady_clock::time_point refreshWaitStart{};
bool waitingForFileChange = false;
std::chrono::steady_clock::time_point fileChangeWaitStart{};
unsigned fileChangeFiredAtArm = 0;
std::string pendingScreenshotName;

// Validation
std::string validationReportPath;

}  // namespace app_state

// Write the current rendered frame to a PNG. Headless has no window, so read
// back the offscreen render texture; windowed uses the macOS window capture.
static void write_screenshot(const std::string& path) {
    if (afterhours::graphics::is_headless()) {
        afterhours::graphics::capture_frame(path);
    } else {
        afterhours::graphics::take_screenshot(path.c_str());
    }
}

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
    log_info("  Window+GPU init: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(t0 - app_state::startTime).count());

    // Needs the window to exist; idempotent, and a no-op without the build
    // opt-in or off macOS.
    afterhours::gestures::install_pinch_monitor();

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

    entity.addComponent<ecs::LayoutComponent>();
    entity.addComponent<ecs::MenuComponent>();

    auto& cmdLog = entity.addComponent<ecs::CommandLogComponent>();
    entity.addComponent<ecs::NetworkOpsComponent>();

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
            tab.get<ecs::Tab>().label = ecs::repo_display_name(path);
        }

        tab.addComponent<ecs::CommitDetailCache>();
        tab.addComponent<ecs::BranchDialogState>();
        auto& review = tab.addComponent<ecs::ReviewComponent>();
        // Restore any saved ballroom review for this repo (survives restart).
        if (!path.empty() && !app_state::testModeEnabled)
            review_store::load_review(path, review);

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

    // Wire git log callback to record all git commands in the CommandLogComponent.
    // The callback fires on detached git worker threads, so it must NOT touch the
    // ECS directly (see drain_git_log): stage entries under a mutex and let the
    // main thread drain them into cmdLog each frame.
    g_cmdLogSink = &cmdLog;
    git::set_log_callback([](const std::string& cmd,
                            const std::string& out,
                            const std::string& err,
                            bool success) {
        auto now = std::chrono::system_clock::now();
        double ts = static_cast<double>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
        std::lock_guard<std::mutex> lock(g_gitLogMutex);
        g_pendingGitLog.push_back({cmd, out, err, success, ts});
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
        // Before LayoutUpdateSystem: ui_scale feeds every size it resolves,
        // so a zoom applied after it lands one frame late.
        sm.register_update_system(std::make_unique<ecs::ZoomSystem>());
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
        sm.register_update_system(std::make_unique<ecs::NetworkOpsPollingSystem>());

        // Toast notification systems. Lift toasts above the bottom status bar
        // so they don't overlap it (afterhours' default sits at the very edge).
        afterhours::toast::PADDING = afterhours::ui::h720(40.0f);
        ui_imm::registerToastSystems(sm);

        // Modal dialog systems
        ui_imm::registerModalSystems(sm);

        // E2E testing systems (only in test mode)
        if (app_state::testModeEnabled) {
            // Land every animation on its final value on the first update, so
            // a screenshot is of a settled frame and a scroll assertion reads
            // where the view IS rather than where it is easing to. Scripts can
            // still say enable_animations to test an animation itself.
            afterhours::animation::set_instant(true);
            if (app_state::e2eNoResize) {
                sm.register_update_system(std::make_unique<SkipResizeCommand>());
            }
            sm.register_update_system(std::make_unique<HandleMakeTestRepo>());
            sm.register_update_system(std::make_unique<HandleResetUI>());
            sm.register_update_system(std::make_unique<HandleTabCommands>());
            sm.register_update_system(std::make_unique<HandleTouchFile>());
            sm.register_update_system(std::make_unique<HandleWaitForRefresh>());
            sm.register_update_system(std::make_unique<HandleWaitForFileChange>());
            sm.register_update_system(std::make_unique<HandleFileWatcherToggle>());
            sm.register_update_system(std::make_unique<HandleBenchFrames>());
            {
                namespace perf = afterhours::testing::perf_commands;
                perf::builtin_profile::enable();
                perf::PerfProvider p;
                p.get_fps = []() -> std::optional<float> {
                    auto avg = e2e_bench::avg_ms();
                    if (!avg || *avg <= 0.0f) return std::nullopt;
                    return 1000.0f / *avg;
                };
                p.get_p99_ms = []() { return e2e_bench::percentile_ms(0.99); };
                p.top_entries = [](int count) {
                    std::vector<perf::PerfEntry> out;
                    for (const auto& [name, acc] : perf::builtin_profile::totals())
                        out.push_back(perf::PerfEntry{
                            name,
                            acc.calls ? static_cast<float>(acc.total_ms / acc.calls) : 0.f,
                            std::nullopt});
                    std::sort(out.begin(), out.end(),
                              [](const auto& a, const auto& b) { return a.ms > b.ms; });
                    if (count > 0 && static_cast<int>(out.size()) > count)
                        out.resize(static_cast<size_t>(count));
                    return out;
                };
                perf::set_provider(std::move(p));
                perf::register_perf_commands(sm);
            }
            afterhours::testing::register_builtin_handlers(sm);
            sm.register_update_system(
                std::make_unique<afterhours::testing::HandleScreenshotCommand>(
                    [](const std::string& name) {
                        std::filesystem::path dir =
                            std::filesystem::absolute(app_state::screenshotDir);
                        std::filesystem::create_directories(dir);
                        std::filesystem::path path = dir / (name + ".png");
                        write_screenshot(path.string());
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

    // Init-time mutations (recent repos, open tabs) reach disk through the
    // cleanup write and the auto-save that follows any later change; a write
    // here cost a synchronous file write on every launch, which the endpoint
    // security stack on this machine turns into tens of milliseconds.
    Settings::get().auto_save_enabled = true;

    auto t2 = std::chrono::high_resolution_clock::now();
    log_info("  Systems registration: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

#ifdef __APPLE__
    // Headless is now truly windowless (offscreen Metal, no sokol_app), so there
    // is no window to hide/activate. Only the windowed test path needs focus.
    if (!app_state::headless && app_state::testModeEnabled) {
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

// Deferred screenshot: captured after multiple frames so the window
// compositor has time to present the rendered content.
static std::string s_readyScreenshotName;
static int s_screenshotDelayFrames = 0;
static constexpr int SCREENSHOT_DELAY = 3;

// Process E2E commands in a tight loop without rendering, breaking when
// a screenshot is needed or when we must wait for async operations.
static void e2e_tick_loop([[maybe_unused]] float real_dt) {
    constexpr int MAX_TICKS = 200;
    constexpr float SIM_DT = 1.0f / 60.0f;

    for (int i = 0; i < MAX_TICKS; ++i) {
        // Wait for deferred screenshot to be captured before advancing
        if (!s_readyScreenshotName.empty()) break;
        if (e2e_bench::requested > 0) break;

        afterhours::testing::test_input::reset_frame();

        if (e2e_refresh_gate::triggered) {
            e2e_refresh_gate::triggered = false;
            app_state::waitingForRefresh = true;
            app_state::refreshWaitStart = std::chrono::steady_clock::now();
        }

        if (app_state::waitingForRefresh) {
            // Wall clock: headless real_dt is a fixed 1/60 s whatever the frame
            // took, so summing it capped this wait at 300 frames, which a
            // git status over a few thousand files outlasts.
            constexpr auto MAX_REFRESH_WAIT = std::chrono::seconds(30);
            bool refreshDone = true;
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (repo) {
                refreshDone = !repo->refreshRequested && !repo->isRefreshing;
            }
            const auto waited =
                std::chrono::steady_clock::now() - app_state::refreshWaitStart;
            if (!refreshDone && waited > MAX_REFRESH_WAIT) {
                log_warn("wait_for_refresh: refresh still running after {} s",
                         MAX_REFRESH_WAIT.count());
            }
            if (refreshDone || waited > MAX_REFRESH_WAIT) {
                app_state::waitingForRefresh = false;
                continue;
            }
            break;
        }

        if (e2e_refresh_gate::file_change_triggered) {
            e2e_refresh_gate::file_change_triggered = false;
            app_state::waitingForFileChange = true;
            app_state::fileChangeWaitStart = std::chrono::steady_clock::now();
            app_state::fileChangeFiredAtArm =
                app_state::fileWatcher ? app_state::fileWatcher->fired : 0;
        }

        if (app_state::waitingForFileChange) {
            // Wall clock, not real_dt: headless frames report a fixed 1/60 s
            // whatever they actually took. Systems keep running once per
            // rendered frame while we break out here, so the watcher polls.
            constexpr auto MAX_FILE_CHANGE_WAIT = std::chrono::seconds(10);
            const bool fired = app_state::fileWatcher &&
                app_state::fileWatcher->fired > app_state::fileChangeFiredAtArm;
            bool refreshDone = true;
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (repo) {
                refreshDone = !repo->refreshRequested && !repo->isRefreshing;
            }
            const auto waited =
                std::chrono::steady_clock::now() - app_state::fileChangeWaitStart;
            if ((fired && refreshDone) || waited > MAX_FILE_CHANGE_WAIT) {
                if (!fired) {
                    log_warn("wait_for_file_change: watcher did not fire within {} s",
                             MAX_FILE_CHANGE_WAIT.count());
                }
                app_state::waitingForFileChange = false;
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
        if (afterhours::EntityQuery()
                .whereHasComponent<afterhours::testing::PendingE2ECommand>()
                .has_values()) break;
    }
}

static void run_bench_frames(float dt) {
    namespace perf = afterhours::testing::perf_commands;
    const int n = e2e_bench::requested;
    e2e_bench::requested = 0;
    perf::builtin_profile::reset();
    e2e_bench::samples_ms.clear();
    e2e_bench::samples_ms.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        afterhours::testing::test_input::reset_frame();
        afterhours::graphics::begin_drawing();
        afterhours::graphics::clear_background(afterhours::Color{30, 30, 30, 255});
        app_state::systemManager->run(dt);
        afterhours::graphics::end_drawing();
        e2e_bench::samples_ms.push_back(
            std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - t0).count());
    }

    const size_t entities = afterhours::EntityHelper::get_entities().size();
    log_info("bench_frames: {} frames, {} entities: avg {:.2f} ms  p50 {:.2f}  p99 {:.2f}  max {:.2f}",
             n, entities, *e2e_bench::avg_ms(), *e2e_bench::percentile_ms(0.50),
             *e2e_bench::percentile_ms(0.99), *e2e_bench::percentile_ms(1.0));

    std::vector<std::pair<std::string, double>> top;
    for (const auto& [name, acc] : perf::builtin_profile::totals())
        top.emplace_back(name, acc.total_ms / static_cast<double>(n));
    std::sort(top.begin(), top.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (top.size() > 8) top.resize(8);
    for (const auto& [name, ms] : top)
        log_info("  {:6.2f} ms/frame  {}", ms, name);
}

// Render one frame and manage deferred screenshots.
// Screenshots are deferred by one frame so the window compositor
// has time to present the rendered content before screencapture runs.
static void e2e_render_and_screenshot(float dt) {
    // Take deferred screenshot after enough frames for the compositor
    if (!s_readyScreenshotName.empty()) {
        if (s_screenshotDelayFrames <= 0) {
            std::filesystem::path dir =
                std::filesystem::absolute(app_state::screenshotDir);
            std::filesystem::create_directories(dir);
            std::filesystem::path path = dir / (s_readyScreenshotName + ".png");
            write_screenshot(path.string());
            s_readyScreenshotName.clear();
        } else {
            s_screenshotDelayFrames--;
        }
    }

    afterhours::testing::test_input::reset_frame();
    afterhours::graphics::begin_drawing();
    afterhours::graphics::clear_background(afterhours::Color{30, 30, 30, 255});
    app_state::systemManager->run(dt);
    afterhours::graphics::end_drawing();

    // Queue for capture after SCREENSHOT_DELAY frames
    if (!app_state::pendingScreenshotName.empty()) {
        s_readyScreenshotName = std::move(app_state::pendingScreenshotName);
        app_state::pendingScreenshotName.clear();
        s_screenshotDelayFrames = SCREENSHOT_DELAY;
    }
}

// Frame callback: runs every frame
static void app_frame() {
    // Move any git-worker-staged log entries into the ECS on the main thread
    // before any system reads them this frame.
    drain_git_log();

    float dt = afterhours::graphics::get_frame_time();

    if (app_state::testModeEnabled &&
        (app_state::e2eRunner.has_commands() || !s_readyScreenshotName.empty())) {
        e2e_tick_loop(dt);
        if (e2e_bench::requested > 0) run_bench_frames(dt);
        e2e_render_and_screenshot(dt);

        if (app_state::e2eRunner.is_finished() && s_readyScreenshotName.empty()) {
#ifdef __APPLE__
            metal_wait_all_screenshots();
#endif
            app_state::e2eRunner.print_results();
            const int code = app_state::e2eRunner.has_failed() ? 1 : 0;
            // Entities before the backend: left to static destruction the
            // backend goes first and entity destructors that still call into
            // it throw bad_variant_access. This is what _exit() used to be
            // dodging -- and _exit() also skipped flushing stdio, so the
            // summary printed one line above never reached the log.
            afterhours::shutdown();
            std::exit(code);
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

    auto* tabStripPtr = ecs::find_singleton<ecs::TabStripComponent>();
    if (tabStripPtr) {
        auto& tabStrip = *tabStripPtr;
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
    {
        // exec -> main: dyld, fixups, static init. Not covered by any timer
        // below, and on a machine with endpoint security it dominates.
        struct proc_bsdinfo bi;
        struct timeval now;
        if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &bi, sizeof bi) == sizeof bi &&
            gettimeofday(&now, nullptr) == 0) {
            long ms = (now.tv_sec - (long)bi.pbi_start_tvsec) * 1000 +
                      (now.tv_usec - (long)bi.pbi_start_tvusec) / 1000;
            log_info("Process start to main: {} ms", ms);
        }
    }
    argh::parser cmdl(argc, argv);

    // Parse repo path from first positional argument
    std::string repoPath;
    cmdl(1, "") >> repoPath;

    // Parse test mode flags
    app_state::testModeEnabled = cmdl["--test-mode"];
    app_state::e2eNoResize = cmdl["--e2e-no-resize"];
    app_state::headless = cmdl["--headless"];
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
        if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) {
            ecs::reset_layout_defaults(*layout);
        }
        if (auto* editor = ecs::find_singleton<ecs::CommitEditorComponent,
                                               ecs::ActiveTab>()) {
            ecs::reset_commit_editor(*editor);
        }
        if (auto* menu = ecs::find_singleton<ecs::MenuComponent>()) {
            ecs::reset_menus(*menu);
        }
        // Global, so nothing above owns it: a script that zooms and does not
        // zoom back would resize the UI for every script after it.
        ui::zoom::reset();
        ui::close_context_menu();
    });
    app_state::e2eRunner.set_property_getter([](const std::string& key) -> std::string {
        // Looked up per key rather than up front: "ui_scale" needs none of
        // these, and each lookup stops at the first match.
        auto layout = [] { return ecs::find_singleton<ecs::LayoutComponent>(); };
        auto repo = [] {
            return ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        };

        if (key == "sidebar_visible") {
            if (auto* l = layout()) return l->sidebarVisible ? "true" : "false";
        } else if (key == "command_log_visible") {
            if (auto* l = layout()) return l->commandLogVisible ? "true" : "false";
        } else if (key == "diff_view_mode") {
            if (auto* l = layout())
                return l->diffViewMode == ecs::LayoutComponent::DiffViewMode::Inline
                           ? "Inline" : "SideBySide";
        } else if (key == "file_view_mode") {
            if (auto* l = layout()) {
                switch (l->fileViewMode) {
                    case ecs::LayoutComponent::FileViewMode::Flat: return "Flat";
                    case ecs::LayoutComponent::FileViewMode::Tree: return "Tree";
                    case ecs::LayoutComponent::FileViewMode::All: return "All";
                    default: return "Unknown";
                }
            }
        } else if (key == "sidebar_width") {
            if (auto* l = layout())
                return std::format("{:.0f}", l->sidebarWidth);
        } else if (key == "sidebar_mode") {
            if (auto* l = layout())
                return l->sidebarMode == ecs::LayoutComponent::SidebarMode::Changes
                           ? "Changes" : "Refs";
        } else if (key == "staged_count") {
            if (auto* r = repo()) return std::to_string(r->stagedFiles.size());
        } else if (key == "unstaged_count") {
            if (auto* r = repo()) return std::to_string(r->unstagedFiles.size());
        } else if (key == "untracked_count") {
            if (auto* r = repo()) return std::to_string(r->untrackedFiles.size());
        } else if (key == "branch") {
            if (auto* r = repo()) return r->currentBranch;
        } else if (key == "selected_file") {
            if (auto* r = repo()) return r->selectedFilePath;
        } else if (key == "ui_scale") {
            // Two decimals: the value is a float the pinch multiplies into, so
            // an exact-match assertion needs a rounded, stable spelling.
            return std::format("{:.2f}", ui::zoom::get());
        } else if (key == "selected_commit") {
            if (auto* r = repo()) return r->selectedCommitHash;
        } else if (key == "is_amend") {
            if (auto* e = ecs::find_singleton<ecs::CommitEditorComponent,
                                              ecs::ActiveTab>())
                return e->isAmend ? "true" : "false";
        } else if (key == "refresh_requested") {
            if (auto* r = repo()) return r->refreshRequested ? "true" : "false";
        } else if (key == "tab_count") {
            auto* ts = ecs::find_singleton<ecs::TabStripComponent>();
            return ts ? std::to_string(ts->tabOrder.size()) : "0";
        } else if (key == "active_tab_label") {
            if (auto* t = ecs::find_singleton<ecs::Tab, ecs::ActiveTab>())
                return t->label;
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

    if (app_state::headless) {
        // True windowless rendering: no sokol_app, no WindowServer. Bootstrap an
        // offscreen Metal context and pump frames manually (graphics::run would
        // create a window via sapp_run).
        afterhours::graphics::Config gc;
        gc.display = afterhours::graphics::DisplayMode::Headless;
        gc.width = cfg.width;
        gc.height = cfg.height;
        if (!afterhours::graphics::init(gc)) {
            fprintf(stderr, "Error: headless graphics init failed\n");
            return 1;
        }
        app_init();

        if (app_state::testModeEnabled) {
            // app_frame drives the e2e runner and calls _exit() on completion.
            // The frame budget is a hang backstop; reaching the end is a failure.
            constexpr int kMaxHeadlessFrames = 200000;
            for (int i = 0; i < kMaxHeadlessFrames; ++i) {
                // One autorelease pool per frame, as sokol_app would provide.
                metal_headless_frame(app_frame);
            }
            fprintf(stderr,
                    "Error: headless e2e did not finish within frame budget\n");
            return 1;
        }

        // No test script: render a few frames so the offscreen texture holds a
        // real frame (useful for a one-off manual capture), then exit cleanly.
        for (int i = 0; i < 3; ++i) {
            metal_headless_frame(app_frame);
        }
        app_cleanup();
        afterhours::shutdown();
        return 0;
    }

    metal_draw_first_frame_early();
    afterhours::graphics::run(cfg);

    return 0;
}
