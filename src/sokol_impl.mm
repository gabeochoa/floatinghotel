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

// Screenshot support (macOS screencapture via window ID)
#import <AppKit/AppKit.h>

extern "C" void metal_set_window_size(int width, int height) {
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
            NSLog(@"metal_set_window_size: no window available");
            return;
        }

        // Get the current frame and compute the new one.
        // Keep the top-left corner anchored (macOS uses bottom-left origin).
        NSRect frame = [window frame];
        CGFloat titleBarHeight = frame.size.height - [[window contentView] frame].size.height;
        CGFloat newHeight = (CGFloat)height + titleBarHeight;
        CGFloat newWidth = (CGFloat)width;

        // Anchor top-left: adjust origin.y so the top edge stays put
        CGFloat deltaH = newHeight - frame.size.height;
        NSRect newFrame = NSMakeRect(frame.origin.x, frame.origin.y - deltaH,
                                     newWidth, newHeight);
        [window setFrame:newFrame display:YES animate:NO];
    }
}

#import <objc/runtime.h>

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
