#define SOKOL_METAL
#define SOKOL_NO_ENTRY
#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#import <AppKit/AppKit.h>
#import <MetalKit/MetalKit.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

extern "C" void metal_defer_window_presentation();
extern "C" void metal_wait_for_gpu();
static int frames = 0;
static bool scheduled = false;
static id<MTLTexture> last_texture;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        metal_wait_for_gpu();
        sg_shutdown();
        std::exit(1);
    }
}

static void init() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    MTKView* view = (MTKView*)window.contentView;
    view.framebufferOnly = NO;
}

static void verify_pixels() {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    id<MTLBuffer> buffer = [last_texture.device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    [blit copyFromTexture:last_texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(1, 1, 1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    const auto* bytes = static_cast<const unsigned char*>(buffer.contents);
    require(bytes[0] >= 24 && bytes[0] <= 27 && bytes[1] >= 21 && bytes[1] <= 24 &&
        bytes[2] >= 19 && bytes[2] <= 22 && bytes[3] == 255, "resized drawable contains a blank or incorrect frame");
}

static void resize_journey() {
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    MTKView* view = (MTKView*)window.contentView;
    view.paused = YES;
    require([NSRunLoop.currentRunLoop.currentMode isEqualToString:NSEventTrackingRunLoopMode], "not running in the native tracking loop");
    const std::array<NSSize, 8> sizes{{{700, 600}, {900, 620}, {1200, 650}, {600, 650},
        {480, 700}, {1400, 800}, {800, 700}, {352, 600}}};
    for (auto size : sizes) {
        const int before = frames;
        const auto start = std::chrono::steady_clock::now();
        [window setContentSize:size];
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        require(frames > before, "live resize returned without rendering the new size");
        require(sapp_width() == static_cast<int>(size.width * sapp_dpi_scale()), "frame width differs from native size");
        require(sapp_height() == static_cast<int>(size.height * sapp_dpi_scale()), "frame height differs from native size");
        require(!window.visible, "probe window became visible");
        require(window.backgroundColor.redComponent < 0.15 && window.backgroundColor.greenComponent < 0.15 &&
            window.backgroundColor.blueComponent < 0.15, "exposed window background is not dark");
        verify_pixels();
        std::printf("RESIZE %.0f %.0f %.3f ms frames=%d\n", size.width, size.height, elapsed, frames - before);
    }
    metal_wait_for_gpu();
    last_texture = nil;
    sg_shutdown();
    std::printf("PASS: eight synchronous live resize frames, DPI %.1f, hidden window\n", sapp_dpi_scale());
    std::exit(0);
}

static void frame() {
    sg_pass pass{};
    pass.swapchain = sglue_swapchain();
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)pass.swapchain.metal.current_drawable;
    require(drawable != nil, "missing Metal drawable");
    last_texture = drawable.texture;
    require(drawable.texture.width == static_cast<NSUInteger>(sapp_width()) &&
        drawable.texture.height == static_cast<NSUInteger>(sapp_height()), "drawable does not match layout dimensions");
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.08f, 0.09f, 0.1f, 1.f};
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();
    ++frames;
    if (!scheduled && frames >= 2) {
        scheduled = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSTimer* timer = [NSTimer timerWithTimeInterval:0 repeats:NO block:^(NSTimer*) { resize_journey(); }];
            [NSRunLoop.mainRunLoop addTimer:timer forMode:NSEventTrackingRunLoopMode];
            [NSRunLoop.mainRunLoop runMode:NSEventTrackingRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:5]];
            require(false, "tracking resize probe returned without completing");
        });
    }
}

int main() {
    metal_defer_window_presentation();
    [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidFinishLaunchingNotification
        object:nil queue:nil usingBlock:^(NSNotification*) {
        NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
        MTKView* view = (MTKView*)window.contentView;
        NSTimer* timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 repeats:YES block:^(NSTimer*) {
            if (!scheduled) [view draw];
        }];
        [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
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
