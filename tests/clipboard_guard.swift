import AppKit
import Foundation
import Darwin

func run() -> Int32 {
    let args = CommandLine.arguments
    guard args.count >= 4 else { return 2 }
    let marker = args[2]
    let name = "org.floatinghotel.tests." + marker
    let board = NSPasteboard(name: NSPasteboard.Name(name))
    if args[1] == "read" {
        guard let value = board.string(forType: .string) else {
            fputs("Private test pasteboard has no text\n", stderr)
            return 1
        }
        do {
            let data = try JSONSerialization.data(withJSONObject: ["text": value], options: [.prettyPrinted, .sortedKeys])
            try data.write(to: URL(fileURLWithPath: args[3]), options: .atomic)
            return 0
        } catch {
            fputs("Could not save clipboard evidence\n", stderr)
            return 1
        }
    }
    guard args[1] == "guard", args.count >= 5 else { return 2 }
    defer {
        board.clearContents()
        board.releaseGlobally()
    }
    let process = Process()
    process.executableURL = URL(fileURLWithPath: args[3])
    process.arguments = Array(args.dropFirst(4))
    var environment = ProcessInfo.processInfo.environment
    environment["FH_TEST_CLIPBOARD_MARKER"] = marker
    environment["FH_TEST_PASTEBOARD_NAME"] = name
    process.environment = environment
    do {
        try process.run()
        process.waitUntilExit()
        return process.terminationStatus
    } catch {
        fputs("Could not start clipboard replay\n", stderr)
        return 1
    }
}

exit(run())
