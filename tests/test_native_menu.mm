#include "../src/platform/native_menu.h"

#import <AppKit/AppKit.h>

#include <cassert>
#include <cstdio>

using native_menu::CommandId;

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        NSMenu* original = [[NSMenu alloc] initWithTitle:@"Original"];
        NSApp.mainMenu = original;
        const auto quit = CommandId{1};
        const auto copy = CommandId{2};
        const auto zoom = CommandId{3};
        const auto sidebar = CommandId{4};
        std::vector<native_menu::Menu> menus = {
            {"Edit", {{copy, "Copy", "Cmd+C"}}},
            {"View", {{zoom, "Zoom In", "Cmd+="}, {{}, "", "", false, false, true},
                      {sidebar, "Sidebar", "Cmd+B", true, true}}}
        };
        assert(native_menu::install("Floating Hotel", quit, menus));
        NSMenu* root = NSApp.mainMenu;
        assert(root.numberOfItems == 3);
        assert([[root itemAtIndex:0].title isEqualToString:@"Floating Hotel"]);
        NSMenu* edit = [root itemAtIndex:1].submenu;
        NSMenu* view = [root itemAtIndex:2].submenu;
        assert([[edit itemAtIndex:0].title isEqualToString:@"Copy"]);
        assert([[edit itemAtIndex:0].keyEquivalent isEqualToString:@"c"]);
        assert([edit itemAtIndex:0].keyEquivalentModifierMask == NSEventModifierFlagCommand);
        assert([view itemAtIndex:1].separatorItem);
        assert([view itemAtIndex:2].state == NSControlStateValueOn);
        [edit performActionForItemAtIndex:0];
        assert((native_menu::drain_commands() == std::vector<native_menu::Invocation>{{copy, ""}}));
        assert(native_menu::drain_commands().empty());
        menus[0].items[0].enabled = false;
        menus[1].items[2].checked = false;
        native_menu::refresh(menus);
        assert(root == NSApp.mainMenu);
        assert(![edit itemAtIndex:0].enabled);
        assert([view itemAtIndex:2].state == NSControlStateValueOff);
        [NSApp sendAction:[edit itemAtIndex:0].action to:[edit itemAtIndex:0].target from:[edit itemAtIndex:0]];
        assert(native_menu::drain_commands().empty());
        menus[0].items[0].enabled = true;
        native_menu::refresh(menus);
        for (NSMenu* candidate in @[root, edit]) {
            int fallback_dispatches = 0;
            NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:0 context:nil
                characters:@"c" charactersIgnoringModifiers:@"c" isARepeat:NO keyCode:8];
            if (![candidate performKeyEquivalent:event]) ++fallback_dispatches;
            assert(fallback_dispatches == 1);
            assert(native_menu::drain_commands().empty());
        }
        NSEvent* zoom_event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:0 context:nil
            characters:@"=" charactersIgnoringModifiers:@"=" isARepeat:NO keyCode:24];
        assert(![root performKeyEquivalent:zoom_event]);
        assert(![view performKeyEquivalent:zoom_event]);
        assert(native_menu::drain_commands().empty());
        NSEvent* quit_event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:0 context:nil
            characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
        assert([root performKeyEquivalent:quit_event]);
        assert((native_menu::drain_commands() == std::vector<native_menu::Invocation>{{quit, ""}}));
        assert(native_menu::drain_commands().empty());
        [[root itemAtIndex:0].submenu performActionForItemAtIndex:0];
        assert((native_menu::drain_commands() == std::vector<native_menu::Invocation>{{quit, ""}}));
        [edit performActionForItemAtIndex:0];
        [view performActionForItemAtIndex:0];
        [edit performActionForItemAtIndex:0];
        const std::vector<native_menu::Invocation> expected_order{{copy, ""}, {zoom, ""}, {copy, ""}};
        assert(native_menu::drain_commands() == expected_order);
        menus[0].items[0].owner = "repo-a:1";
        native_menu::refresh(menus);
        [edit performActionForItemAtIndex:0];
        menus[0].items[0].owner = "repo-b:2";
        native_menu::refresh(menus);
        auto queued = native_menu::drain_commands();
        assert(queued.size() == 1 && queued[0].owner == "repo-a:1");
        assert(queued[0].owner != menus[0].items[0].owner);
        assert(NSApp.windows.count == 0);
        assert(NSApp.activationPolicy == NSApplicationActivationPolicyProhibited);
        native_menu::shutdown();
        assert(NSApp.mainMenu == original);
        assert(native_menu::install("Floating Hotel", quit, menus));
        native_menu::shutdown();
        assert(NSApp.mainMenu == original);
        native_menu::shutdown();
        [original release];
        std::puts("PASS: native menu hierarchy, state, action queue, shortcut pass-through, Quit, and windowless lifecycle");
    }
}
