// Real window-server input for the native session check: menu-bar clicks,
// a Cmd chord, and a corner drag, posted as CGEvents so AppKit's menu
// tracking loop and live-resize loop run exactly as for a person.
// Needs Accessibility permission for the terminal running it.
//
// post_events trusted
// post_events menu <pid> <menu title> <item title>
// post_events chord <pid> <key code> cmd
// post_events drag <pid> <dx> <dy> <steps>
import AppKit
import ApplicationServices
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

func attribute<T>(_ element: AXUIElement, _ name: String) -> T? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else { return nil }
    return value as? T
}

func frame(_ element: AXUIElement) -> CGRect? {
    guard let position: AXValue = attribute(element, kAXPositionAttribute),
          let size: AXValue = attribute(element, kAXSizeAttribute) else { return nil }
    var point = CGPoint.zero, extent = CGSize.zero
    AXValueGetValue(position, .cgPoint, &point)
    AXValueGetValue(size, .cgSize, &extent)
    return CGRect(origin: point, size: extent)
}

func child(_ element: AXUIElement, titled title: String) -> AXUIElement? {
    let children: [AXUIElement] = attribute(element, kAXChildrenAttribute) ?? []
    for item in children {
        if let name: String = attribute(item, kAXTitleAttribute), name == title { return item }
        // Menu bar items hold an AXMenu whose children are the items.
        if let found = child(item, titled: title) { return found }
    }
    return nil
}

func click(at point: CGPoint) {
    for type in [CGEventType.mouseMoved, .leftMouseDown, .leftMouseUp] {
        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(60_000)
    }
}

let args = CommandLine.arguments
guard args.count >= 2 else { fail("usage: post_events trusted|menu|chord|drag ...") }
if args[1] == "trusted" { print(AXIsProcessTrusted() ? "yes" : "no"); exit(0) }
guard AXIsProcessTrusted() else { fail("Accessibility permission is required for the terminal running this check") }
guard args.count >= 3, let pid = pid_t(args[2]) else { fail("missing pid") }
let app = AXUIElementCreateApplication(pid)
NSRunningApplication(processIdentifier: pid)?.activate()
usleep(300_000)

switch args[1] {
case "menu":
    guard args.count == 5, let bar: AXUIElement = attribute(app, kAXMenuBarAttribute),
          let menu = child(bar, titled: args[3]), let menuFrame = frame(menu) else { fail("menu \(args[3]) not found") }
    click(at: CGPoint(x: menuFrame.midX, y: menuFrame.midY))  // opens the menu: AppKit tracking starts
    usleep(400_000)
    guard let item = child(menu, titled: args[4]), let itemFrame = frame(item) else { fail("item \(args[4]) not found") }
    click(at: CGPoint(x: itemFrame.midX, y: itemFrame.midY))  // chosen inside the tracking loop
case "chord":
    guard args.count == 5, let code = CGKeyCode(args[3]) else { fail("usage: chord <pid> <keycode> cmd") }
    for down in [true, false] {
        let event = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down)
        event?.flags = .maskCommand
        event?.post(tap: .cghidEventTap)
        usleep(80_000)
    }
case "drag":
    guard args.count == 6, let dx = Double(args[3]), let dy = Double(args[4]), let steps = Int(args[5]) else { fail("usage: drag <pid> dx dy steps") }
    guard let window: AXUIElement = attribute(app, kAXFocusedWindowAttribute) ?? (attribute(app, kAXWindowsAttribute) as [AXUIElement]?)?.first,
          let bounds = frame(window) else { fail("no window") }
    let start = CGPoint(x: bounds.maxX - 3, y: bounds.maxY - 3)  // bottom-right resize corner
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: start, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(100_000)
    CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: start, mouseButton: .left)?.post(tap: .cghidEventTap)
    for step in 1...steps {
        let point = CGPoint(x: start.x + dx * Double(step) / Double(steps), y: start.y + dy * Double(step) / Double(steps))
        CGEvent(mouseEventSource: nil, mouseType: .leftMouseDragged, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(16_000)
    }
    CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: CGPoint(x: start.x + dx, y: start.y + dy), mouseButton: .left)?.post(tap: .cghidEventTap)
    if let after = frame(window) { print("window \(Int(bounds.width))x\(Int(bounds.height)) -> \(Int(after.width))x\(Int(after.height))") }
default:
    fail("unknown command \(args[1])")
}
