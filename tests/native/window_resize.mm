#define SOKOL_METAL
#define SOKOL_NO_ENTRY
#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>
#import <objc/runtime.h>
#include <array>
#include <cstdio>
#include <cstdlib>

extern "C" void metal_set_window_size(int, int);
extern "C" void metal_defer_window_presentation();
extern "C" void metal_wait_for_gpu();

static constexpr std::array widths{352, 1272, 352, 480, 1400, 480};
static size_t nextWidth = 0;
static int frames = 0;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void init() {
    class_addMethod([NSApp class], @selector(mainWindow), imp_implementationWithBlock(^NSWindow*(id) {
        return (__bridge NSWindow*)sapp_macos_get_window();
    }), "@@:");
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    [NSTimer scheduledTimerWithTimeInterval:10 repeats:NO block:^(NSTimer*) {
        require(false, "native resize test timed out");
    }];
}

static void frame() {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    require(!window.visible, "test window became visible");
    const int width = sapp_width();
    const int height = sapp_height();
    sg_pass pass{};
    pass.swapchain = sglue_swapchain();
    if (frames % 4 == 0 && nextWidth < widths.size()) {
        metal_set_window_size(widths[nextWidth] + 16, 600);
        metal_set_window_size(widths[nextWidth++], 600);
        require(sapp_width() == width && sapp_height() == height,
                "window dimensions changed inside the Metal draw callback");
    } else if (frames % 4 == 1 && nextWidth > 0) {
        require(sapp_width() == static_cast<int>(widths[nextWidth - 1] * sapp_dpi_scale()),
                "resize did not reach its exact target");
    }
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)pass.swapchain.metal.current_drawable;
    require(drawable != nil, "missing drawable");
    require(drawable.texture.width == static_cast<NSUInteger>(width) &&
            drawable.texture.height == static_cast<NSUInteger>(height), "drawable size differs from the frame");
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.08f, 0.09f, 0.1f, 1.f};
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();
    if (++frames == 28) {
        metal_wait_for_gpu();
        sg_shutdown();
        std::printf("PASS: six coalesced resizes outside draw callbacks; 28 matching Metal frames; window stayed hidden; DPI %.1f\n", sapp_dpi_scale());
        std::exit(0);
    }
}

int main() {
    metal_defer_window_presentation();
    [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidFinishLaunchingNotification
        object:nil queue:nil usingBlock:^(NSNotification*) {
        NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
        MTKView* view = (MTKView*)window.contentView;
        [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0 repeats:YES block:^(NSTimer*) {
            [view drawRect:view.bounds];
        }];
    }];
    sapp_desc desc{};
    desc.width = 280;
    desc.height = 600;
    desc.sample_count = 4;
    desc.high_dpi = std::getenv("FH_TEST_RETINA") != nullptr;
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.logger.func = slog_func;
    sapp_run(&desc);
}
