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
        NSString* path = [NSString stringWithUTF8String:filename];

        NSString* cmd = [NSString stringWithFormat:
            @"/usr/sbin/screencapture -x -o -l %u %@",
            windowID, path];
        int ret = system([cmd UTF8String]);
        if (ret != 0) {
            NSLog(@"take_screenshot: screencapture failed with code %d", ret);
        }
    }
}
