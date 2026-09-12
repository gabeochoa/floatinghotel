import AppKit

let args = CommandLine.arguments
guard args.count == 4,
      let before = NSImage(contentsOfFile: args[1]),
      let after = NSImage(contentsOfFile: args[2]) else { exit(2) }
let width = Int(max(before.size.width, after.size.width))
let height = Int(max(before.size.height, after.size.height))
guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil,
    pixelsWide: width * 2, pixelsHigh: height + 48,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
    isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
    let context = NSGraphicsContext(bitmapImageRep: bitmap) else { exit(1) }
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = context
NSColor(calibratedRed: 0.08, green: 0.09, blue: 0.11, alpha: 1).setFill()
NSRect(x: 0, y: 0, width: width * 2, height: height + 48).fill()
let style: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 18, weight: .medium),
    .foregroundColor: NSColor.white
]
"Before".draw(at: NSPoint(x: 20, y: height + 12), withAttributes: style)
"Review focus".draw(at: NSPoint(x: width + 20, y: height + 12), withAttributes: style)
before.draw(in: NSRect(x: 0, y: 0, width: width, height: height))
after.draw(in: NSRect(x: width, y: 0, width: width, height: height))
NSGraphicsContext.restoreGraphicsState()
guard let png = bitmap.representation(using: .png, properties: [:]) else { exit(1) }
do { try png.write(to: URL(fileURLWithPath: args[3])) }
catch { fputs("Could not write comparison: \(error)\n", stderr); exit(1) }
