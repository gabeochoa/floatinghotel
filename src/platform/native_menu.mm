#include "native_menu.h"

#import <AppKit/AppKit.h>

#include <cassert>
#include <unordered_map>
#include <utility>

namespace {
std::vector<native_menu::CommandId> pending_commands;
std::unordered_map<native_menu::CommandId, NSMenuItem*> command_items;
std::vector<native_menu::Menu> displayed_menus;
NSMenu* installed_menu = nil;
NSMenu* previous_menu = nil;

void apply_item(NSMenuItem* target, const native_menu::Item& item) {
    target.title = [NSString stringWithUTF8String:item.title.c_str()];
    target.enabled = item.enabled;
    target.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* shortcut = [NSString stringWithUTF8String:item.shortcut.c_str()];
    NSArray<NSString*>* parts = [shortcut componentsSeparatedByString:@"+"];
    NSEventModifierFlags modifiers = 0;
    for (NSUInteger i = 0; i + 1 < parts.count; ++i) {
        NSString* part = parts[i];
        if ([part isEqualToString:@"Cmd"]) modifiers |= NSEventModifierFlagCommand;
        if ([part isEqualToString:@"Ctrl"]) modifiers |= NSEventModifierFlagControl;
        if ([part isEqualToString:@"Alt"]) modifiers |= NSEventModifierFlagOption;
        if ([part isEqualToString:@"Shift"]) modifiers |= NSEventModifierFlagShift;
    }
    NSString* key = parts.lastObject.lowercaseString;
    if ([key isEqualToString:@"enter"]) key = @"\r";
    if ([key isEqualToString:@"left"]) key = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSLeftArrowFunctionKey)];
    if ([key isEqualToString:@"right"]) key = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSRightArrowFunctionKey)];
    target.keyEquivalent = key;
    target.keyEquivalentModifierMask = modifiers;
}
}

@interface FHApplicationCommands : NSObject
- (void)enqueue:(NSMenuItem*)sender;
@end

@implementation FHApplicationCommands
- (void)enqueue:(NSMenuItem*)sender {
    assert(NSThread.isMainThread);
    if (sender.enabled)
        pending_commands.push_back(static_cast<native_menu::CommandId>(sender.tag));
}
@end

@interface FHShortcutDisplayMenu : NSMenu
@end

@implementation FHShortcutDisplayMenu
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    (void)event;
    return NO;
}
@end

@interface FHRootMenu : NSMenu
@end

@implementation FHRootMenu
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    return [[[self itemAtIndex:0] submenu] performKeyEquivalent:event];
}
@end

namespace {
FHApplicationCommands* action_target = nil;
}

namespace native_menu {

bool install(const std::string& app_title, CommandId quit_command,
             const std::vector<Menu>& menus) {
    assert(NSThread.isMainThread);
    if (!NSApp) return false;
    shutdown();
    previous_menu = [NSApp.mainMenu retain];
    action_target = [[FHApplicationCommands alloc] init];
    installed_menu = [[FHRootMenu alloc] initWithTitle:@""];
    installed_menu.autoenablesItems = NO;
    auto append_menu = [](NSString* title, NSMenu* submenu) {
        NSMenuItem* heading = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        heading.submenu = submenu;
        [installed_menu addItem:heading];
        [heading release];
        [submenu release];
    };
    NSString* app_name = [NSString stringWithUTF8String:app_title.c_str()];
    NSMenu* app_menu = [[NSMenu alloc] initWithTitle:app_name];
    app_menu.autoenablesItems = NO;
    NSMenuItem* quit = [[NSMenuItem alloc]
        initWithTitle:[@"Quit " stringByAppendingString:app_name]
        action:@selector(enqueue:) keyEquivalent:@"q"];
    quit.target = action_target;
    quit.tag = static_cast<NSInteger>(quit_command);
    quit.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    [app_menu addItem:quit];
    [quit release];
    append_menu(app_name, app_menu);
    for (const auto& menu : menus) {
        NSString* title = [NSString stringWithUTF8String:menu.title.c_str()];
        NSMenu* submenu = [[FHShortcutDisplayMenu alloc] initWithTitle:title];
        submenu.autoenablesItems = NO;
        for (const auto& item : menu.items) {
            if (item.separator) {
                [submenu addItem:NSMenuItem.separatorItem];
                continue;
            }
            NSMenuItem* native_item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(enqueue:) keyEquivalent:@""];
            native_item.target = action_target;
            native_item.tag = static_cast<NSInteger>(item.command);
            apply_item(native_item, item);
            const bool inserted = command_items.emplace(item.command, native_item).second;
            assert(inserted && item.command != quit_command);
            [submenu addItem:native_item];
            [native_item release];
        }
        append_menu(title, submenu);
    }
    NSApp.mainMenu = installed_menu;
    displayed_menus = menus;
    return true;
}

void refresh(const std::vector<Menu>& menus) {
    assert(NSThread.isMainThread);
    if (!installed_menu || displayed_menus == menus) return;
    for (const auto& menu : menus)
        for (const auto& item : menu.items)
            if (!item.separator) apply_item(command_items.at(item.command), item);
    displayed_menus = menus;
}

std::vector<CommandId> drain_commands() {
    assert(NSThread.isMainThread);
    return std::exchange(pending_commands, {});
}

void shutdown() {
    assert(NSThread.isMainThread);
    if (installed_menu && NSApp.mainMenu == installed_menu) NSApp.mainMenu = previous_menu;
    command_items.clear();
    displayed_menus.clear();
    pending_commands.clear();
    [installed_menu release];
    installed_menu = nil;
    [previous_menu release];
    previous_menu = nil;
    [action_target release];
    action_target = nil;
}

}
