import AppKit
import Foundation
import Darwin

func run() -> Int32 {
    let args = CommandLine.arguments
    guard args.count >= 4 else { return 2 }
    let board = NSPasteboard.general
    let marker = args[2]
    if args[1] == "read" {
        guard let value = board.string(forType: .string), value.contains(marker) else {
            fputs("Clipboard did not contain this test's marker\n", stderr)
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
    let original = (board.pasteboardItems ?? []).map { item -> NSPasteboardItem in
        let copy = NSPasteboardItem()
        for type in item.types {
            if let data = item.data(forType: type) { copy.setData(data, forType: type) }
        }
        return copy
    }
    defer {
        if board.string(forType: .string)?.contains(marker) == true {
            board.clearContents()
            if !original.isEmpty { board.writeObjects(original) }
        }
    }
    let process = Process()
    process.executableURL = URL(fileURLWithPath: args[3])
    process.arguments = Array(args.dropFirst(4))
    var environment = ProcessInfo.processInfo.environment
    environment["FH_TEST_CLIPBOARD_MARKER"] = marker
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
