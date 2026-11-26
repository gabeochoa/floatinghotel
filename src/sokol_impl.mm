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

#import <objc/runtime.h>

static bool startup_presented = false;
static bool hidden_native_test = false;

static NSString* test_pasteboard_name = nil;

static NSPasteboard* scoped_test_pasteboard(id, SEL) {
    return [NSPasteboard pasteboardWithName:test_pasteboard_name];
}

extern "C" void metal_enable_hidden_test(void) {
    hidden_native_test = true;
    if (const char* name = std::getenv("FH_TEST_PASTEBOARD_NAME")) {
        test_pasteboard_name = [NSString stringWithUTF8String:name];
        Method method = class_getClassMethod([NSPasteboard class], @selector(generalPasteboard));
        method_setImplementation(method, reinterpret_cast<IMP>(scoped_test_pasteboard));
    }
}
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

// Native folder picker for Open Repository (Cmd+O). Runs the panel modal;
// returns false on cancel, otherwise copies the chosen directory path.
extern "C" bool metal_choose_folder(char* out, int out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = NO;
        panel.prompt = @"Open";
        if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
        const char* path = panel.URL.path.UTF8String;
        if (!path) return false;
        std::strncpy(out, path, static_cast<size_t>(out_size) - 1);
        out[out_size - 1] = '\0';
        return true;
    }
}

// Native image picker for ACP prompt attachments. Same shape as the
// folder picker; the caller validates the extension.
extern "C" bool metal_choose_image(char* out, int out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Attach";
        panel.message = @"Choose an image to attach to the prompt";
        if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
        const char* path = panel.URL.path.UTF8String;
        if (!path) return false;
        std::strncpy(out, path, static_cast<size_t>(out_size) - 1);
        out[out_size - 1] = '\0';
        return true;
    }
}

// Live modifier state straight from the OS (bitmask: 1 shift, 2 control,
// 4 option, 8 command). The sokol key_down latch is only cleared by a KEY_UP
// delivered to this app; a modifier released while another app is frontmost
// (Cmd+Shift+4 screenshots, app switching) never produces one, leaving the
// latch stuck down — plain clicks then read as shift/cmd clicks forever.
// The app reconciles the modifier latches against this every frame.
extern "C" int metal_live_modifiers(void) {
    const NSEventModifierFlags flags = [NSEvent modifierFlags];
    int bits = 0;
    if (flags & NSEventModifierFlagShift) bits |= 1;
    if (flags & NSEventModifierFlagControl) bits |= 2;
    if (flags & NSEventModifierFlagOption) bits |= 4;
    if (flags & NSEventModifierFlagCommand) bits |= 8;
    return bits;
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
            const_cast<char*>("/usr/sbin/screencapture"),
            const_cast<char*>("-x"), const_cast<char*>("-o"),
            const_cast<char*>("-l"), wid_str,
            const_cast<char*>(filename),
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

// Afterhours now draws the first frame itself from
// applicationDidFinishLaunching. FloatingHotel still needs this timer because
// its startup gate keeps the window unordered until the initial repository
// refresh settles; without repeated hidden draws, those startup frames do not
// run. Registering the DidFinishLaunching observer from inside
// WillFinishLaunching puts it after sokol's delegate in the notification order.
extern "C" void fh_install_startup_draw_timer(void) {
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    __block id will = nil;
    will = [nc addObserverForName:NSApplicationWillFinishLaunchingNotification
                           object:nil
                            queue:nil
                       usingBlock:^(NSNotification*) {
        [[NSNotificationCenter defaultCenter] removeObserver:will];
        __block id did = nil;
        did = [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidFinishLaunchingNotification
                              object:nil
                               queue:nil
                          usingBlock:^(NSNotification*) {
            [[NSNotificationCenter defaultCenter] removeObserver:did];
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
