import AppKit

for (index, path) in CommandLine.arguments.dropFirst().enumerated() {
    let size = NSSize(width: index == 0 ? 80 : 120, height: index == 0 ? 60 : 80)
    let image = NSImage(size: size)
    image.lockFocus()
    (index == 0 ? NSColor.systemRed : NSColor.systemBlue).setFill()
    NSBezierPath(rect: NSRect(origin: .zero, size: size)).fill()
    image.unlockFocus()
    let bitmap = NSBitmapImageRep(data: image.tiffRepresentation!)!
    try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: path))
}
