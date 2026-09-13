// sokol_impl.mm
// Objective-C++ file that compiles the Sokol implementations for Metal on macOS.
// This must be compiled as a single translation unit with SOKOL_IMPL defined.

#define SOKOL_IMPL
#define SOKOL_METAL
#define SOKOL_NO_ENTRY

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <sokol/sokol_log.h>

// 2D drawing and text rendering
#define SOKOL_GL_IMPL
#include <sokol/sokol_gl.h>

#define FONTSTASH_IMPLEMENTATION
#include <fontstash/stb_truetype.h>
#include <fontstash/fontstash.h>

#define SOKOL_FONTSTASH_IMPL
#include <sokol/sokol_fontstash.h>

// Image decoding (stb_image) rides along with this SOKOL_IMPL TU, and this also
// defines the afterhours sokol-impl sentinel that graphics::run() references.
#include <afterhours/src/backends/sokol/image_decode.h>

// Metal texture readback -> PNG + headless MTLDevice creation. Must follow the
// sokol_gfx impl above (uses sg_mtl_* accessors).
#include <afterhours/src/backends/sokol/capture_impl.h>

// Screenshot support (macOS screencapture via window ID)
#import <AppKit/AppKit.h>
#include <optional>
#include <utility>

static std::optional<std::pair<int, int>> pending_window_size;

extern "C" bool metal_window_resize_pending(void) {
    return pending_window_size.has_value();
}

extern "C" void metal_set_window_size(int width, int height) {
    if (width <= 0 || height <= 0) return;
    const bool scheduled = pending_window_size.has_value();
    pending_window_size = {width, height};
    if (scheduled) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        const auto [targetWidth, targetHeight] = *pending_window_size;
        NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
        if (!window) {
            pending_window_size.reset();
            NSLog(@"metal_set_window_size: no window available");
            return;
        }

        // Get the current frame and compute the new one.
        // Keep the top-left corner anchored (macOS uses bottom-left origin).
        NSRect frame = [window frame];
        CGFloat titleBarHeight = frame.size.height - [[window contentView] frame].size.height;
        CGFloat newHeight = (CGFloat)targetHeight + titleBarHeight;
        CGFloat newWidth = (CGFloat)targetWidth;

        // Dark window background so any area exposed during a resize doesn't
        // flash white before the app redraws it.
        window.backgroundColor = [NSColor colorWithSRGBRed:0.1176
                                                     green:0.1176
                                                      blue:0.1176
                                                     alpha:1.0];

        // CRITICAL: pin the Metal view's layer to the top-left corner (no scale)
        // for this resize. Sokol renders into an MTKView whose layerContentsPlacement
        // defaults to a *scaling* value, so on a programmatic setFrame AppKit
        // stretches the last-drawn frame (sidebar included) to fill the new bounds
        // until the next frame is drawn — that's the sidebar "moving/resizing".
        // Anchoring top-left keeps the old frame 1:1 where the sidebar lives; the
        // newly exposed strip just shows the dark bg until the next redraw.
        // (Same workaround sokol applies for user drags; see sokol #700/#727.)
        NSView* contentView = [window contentView];
        if (contentView.layer) {
            contentView.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;
        }

        // Anchor top-left: adjust origin.y so the top edge stays put.
        // Instant (animate:NO): the animated variant desyncs from the Metal
        // drawable, causing a white flash on grow and content-snap on shrink.
        CGFloat deltaH = newHeight - frame.size.height;
        NSRect newFrame = NSMakeRect(frame.origin.x, frame.origin.y - deltaH,
                                     newWidth, newHeight);
        [window setFrame:newFrame display:NO animate:NO];
        pending_window_size.reset();
        [contentView setNeedsDisplay:YES];
    });
}

#import <objc/runtime.h>

static void (*window_did_resize)(id, SEL, NSNotification*);
static bool drawing_for_resize = false;

static void resize_and_draw(id delegate, SEL selector, NSNotification* notification) {
    window_did_resize(delegate, selector, notification);
    NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
    MTKView* view = (MTKView*)window.contentView;
    window.backgroundColor = [NSColor colorWithSRGBRed:0.0824 green:0.0902 blue:0.1059 alpha:1.0];
    view.layer.backgroundColor = window.backgroundColor.CGColor;
    if (!_sapp.valid || _sapp.first_frame || pending_window_size || drawing_for_resize) return;
    drawing_for_resize = true;
    const double start = CACurrentMediaTime();
    [view draw];
    drawing_for_resize = false;
    if (std::getenv("FH_RESIZE_TIMING"))
        fprintf(stdout, "[INFO] RESIZE frame=%dx%d elapsed_ms=%.3f\n", sapp_width(), sapp_height(),
            (CACurrentMediaTime() - start) * 1000.0);
}

static bool startup_presented = false;
static bool hidden_native_test = false;

extern "C" void metal_enable_hidden_test(void) { hidden_native_test = true; }
static bool startup_submitted = false;
static NSTimer* startup_draw_timer = nil;
static void (*startup_order_window)(id, SEL, NSWindowOrderingMode, NSInteger);
static void (*startup_make_key)(id, SEL, id);
static BOOL (*startup_can_become_key)(id, SEL);

@interface FloatingHotelApplication : NSApplication
@end

@implementation FloatingHotelApplication
- (BOOL)setActivationPolicy:(NSApplicationActivationPolicy)policy {
    return [super setActivationPolicy:startup_presented && !hidden_native_test ? policy : NSApplicationActivationPolicyProhibited];
}
- (void)activateIgnoringOtherApps:(BOOL)flag {
    if (startup_presented && !hidden_native_test) [super activateIgnoringOtherApps:flag];
}
- (void)activate {
    if (startup_presented && !hidden_native_test) [super activate];
}
@end

static void startup_order(id window, SEL selector, NSWindowOrderingMode mode, NSInteger relative) {
    if ((startup_presented && !hidden_native_test) || mode == NSWindowOut)
        startup_order_window(window, selector, mode, relative);
}

static void startup_make_key_and_order(id window, SEL selector, id sender) {
    if (startup_presented && !hidden_native_test) startup_make_key(window, selector, sender);
}

static BOOL startup_can_key(id window, SEL selector) {
    return startup_presented && !hidden_native_test && startup_can_become_key(window, selector);
}

extern "C" void metal_defer_window_presentation(void) {
    [FloatingHotelApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    if (std::getenv("FH_NAVIGATION_TIMING"))
        fprintf(stdout, "[INFO] NAV application_class=%s\n", class_getName([NSApp class]));
    Method resize = class_getInstanceMethod([_sapp_macos_window_delegate class], @selector(windowDidResize:));
    window_did_resize = reinterpret_cast<decltype(window_did_resize)>(method_getImplementation(resize));
    method_setImplementation(resize, reinterpret_cast<IMP>(resize_and_draw));
    Class windowClass = [_sapp_macos_window class];
    Method order = class_getInstanceMethod(windowClass, @selector(orderWindow:relativeTo:));
    startup_order_window = reinterpret_cast<decltype(startup_order_window)>(method_getImplementation(order));
    class_addMethod(windowClass, @selector(orderWindow:relativeTo:),
        reinterpret_cast<IMP>(startup_order), method_getTypeEncoding(order));
    Method key = class_getInstanceMethod(windowClass, @selector(makeKeyAndOrderFront:));
    startup_make_key = reinterpret_cast<decltype(startup_make_key)>(method_getImplementation(key));
    class_addMethod(windowClass, @selector(makeKeyAndOrderFront:),
        reinterpret_cast<IMP>(startup_make_key_and_order), method_getTypeEncoding(key));
    Method canKey = class_getInstanceMethod(windowClass, @selector(canBecomeKeyWindow));
    startup_can_become_key = reinterpret_cast<decltype(startup_can_become_key)>(method_getImplementation(canKey));
    class_addMethod(windowClass, @selector(canBecomeKeyWindow),
        reinterpret_cast<IMP>(startup_can_key), method_getTypeEncoding(canKey));
}

extern "C" bool metal_startup_presented(void) {
    if (!startup_presented && std::getenv("FH_NAVIGATION_TIMING")) {
        static bool logged = false;
        NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
        if (!logged) {
            fprintf(stdout, "[INFO] NAV hidden visible=%d key=%d active=%d\n",
                window.isVisible, window.isKeyWindow, NSApp.isActive);
            logged = true;
        }
        if (window.isVisible || window.isKeyWindow)
            fprintf(stdout, "[ERROR] NAV premature_window\n");
        if (NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == getpid())
            fprintf(stdout, "[ERROR] NAV premature_focus\n");
    }
    return startup_presented;
}

extern "C" void metal_present_ready_frame(void) {
    if (startup_submitted) return;
    startup_submitted = true;
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    id<MTLCommandBuffer> fence = [queue commandBuffer];
    [fence addCompletedHandler:^(id<MTLCommandBuffer>) {
        dispatch_async(dispatch_get_main_queue(), ^{
            NSWindow* window = (__bridge NSWindow*)sapp_macos_get_window();
            fprintf(stdout, "[INFO] NAV before_present visible=%d key=%d active=%d frontmost=%d policy=%ld\n",
                window.isVisible, window.isKeyWindow, NSApp.isActive,
                NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == getpid(),
                static_cast<long>(NSApp.activationPolicy));
            startup_presented = true;
            if (hidden_native_test) {
                fprintf(stdout, "[INFO] Native test window hidden=%d key=%d\n", !window.isVisible, window.isKeyWindow);
                return;
            }
            [startup_draw_timer invalidate];
            startup_draw_timer = nil;
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];
            [window makeKeyAndOrderFront:nil];
            fprintf(stdout, "[INFO] NAV presented visible=%d key=%d\n", window.isVisible, window.isKeyWindow);
        });
    }];
    [fence commit];
}

extern "C" void metal_wait_for_gpu(void) {
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    id<MTLCommandBuffer> fence = [queue commandBuffer];
    [fence commit];
    [fence waitUntilCompleted];
}

static id _e2e_activity_token = nil;

extern "C" void metal_activate_app(void) {
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (window) {
            [window makeKeyAndOrderFront:nil];
        }

        if (!_e2e_activity_token) {
            _e2e_activity_token = [[NSProcessInfo processInfo]
                beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep |
                                         NSActivityLatencyCritical)
                reason:@"E2E Testing"];
        }
    }
}

#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

extern "C" void metal_take_screenshot(const char* filename) {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            window = [NSApp keyWindow];
        }
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) {
                    window = w;
                    break;
                }
            }
        }
        if (!window) {
            NSLog(@"take_screenshot: no window available");
            return;
        }

        CGWindowID windowID = (CGWindowID)[window windowNumber];
        char wid_str[32];
        snprintf(wid_str, sizeof(wid_str), "%u", windowID);

        char* argv[] = {
            (char*)"/usr/sbin/screencapture",
            (char*)"-x", (char*)"-o",
            (char*)"-l", wid_str,
            (char*)filename,
            nullptr
        };

        pid_t pid;
        int ret = posix_spawn(&pid, "/usr/sbin/screencapture",
                              nullptr, nullptr, argv, environ);
        if (ret == 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            NSLog(@"take_screenshot: posix_spawn failed with %d", ret);
        }
    }
}

extern "C" void metal_wait_all_screenshots(void) {
    // no-op: screenshots are taken synchronously
}

static bool _headless_mode = false;

extern "C" void metal_hide_window(void) {
    @autoreleasepool {
        _headless_mode = true;

        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            NSArray<NSWindow*>* windows = [NSApp windows];
            for (NSWindow* w in windows) {
                if ([w isVisible]) { window = w; break; }
            }
        }
        if (window) {
            // Move off-screen rather than orderOut: so the Metal display
            // link keeps firing at full speed and screencapture -l still
            // works (captures the window's backing store by window ID).
            NSRect frame = [window frame];
            [window setFrame:NSMakeRect(-20000, -20000, frame.size.width, frame.size.height)
                     display:YES animate:NO];
        }

        // Suppress idle sleep throttling even though the window is off-screen
        if (!_e2e_activity_token) {
            _e2e_activity_token = [[NSProcessInfo processInfo]
                beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep |
                                         NSActivityLatencyCritical)
                reason:@"E2E Headless Testing"];
        }
    }
}

extern "C" bool metal_is_headless(void) {
    return _headless_mode;
}

// Headless has no sokol_app, and sokol_app is what wraps every frame in an
// autorelease pool. Without one, the command buffers and encoders sokol_gfx
// autoreleases each frame are never freed until exit.
extern "C" void metal_headless_frame(void (*fn)(void)) {
    @autoreleasepool {
        fn();
    }
}

// sokol_app only runs init_cb (and so the first frame) when MTKView's display
// link delivers its first drawRect, 65-160ms after the window is up on a loaded
// machine. Draw once ourselves as soon as sokol's applicationDidFinishLaunching
// has built the window. Registering the DidFinishLaunching observer from inside
// WillFinishLaunching puts it after sokol's delegate in the notification order.
extern "C" void metal_draw_first_frame_early(void) {
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    __block id will = nil;
    will = [nc addObserverForName:NSApplicationWillFinishLaunchingNotification
                           object:nil
                            queue:nil
                       usingBlock:^(NSNotification*) {
        [nc removeObserver:will];
        __block id did = nil;
        did = [nc addObserverForName:NSApplicationDidFinishLaunchingNotification
                              object:nil
                               queue:nil
                          usingBlock:^(NSNotification*) {
            [nc removeObserver:did];
            NSWindow* w = (__bridge NSWindow*)sapp_macos_get_window();
            MTKView* v = (MTKView*)[w contentView];
            if (v) {
                w.backgroundColor = [NSColor colorWithSRGBRed:0.0824 green:0.0902 blue:0.1059 alpha:1.0];
                v.layer.backgroundColor = w.backgroundColor.CGColor;
                startup_draw_timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                    repeats:YES block:^(NSTimer*) { [v draw]; }];
                [v draw];
                fprintf(stdout, "[INFO] First frame drawn from applicationDidFinishLaunching\n");
            }
        }];
    }];
}
