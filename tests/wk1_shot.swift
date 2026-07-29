import Cocoa
import WebKit

// Legacy WebKit1 WebView renders IN-PROCESS (no WebContent XPC subprocess),
// which is why it works where WKWebView/Chrome cannot in this sandbox.
// Usage: wk1_shot <file-url> <out.png> [w] [h] [js]

let args = CommandLine.arguments
guard args.count >= 3 else { exit(2) }
let urlStr = args[1]
let outPath = args[2]
let width = args.count > 3 ? Int(args[3]) ?? 1280 : 1280
let height = args.count > 4 ? Int(args[4]) ?? 800 : 800
let js = args.count > 5 ? args[5] : ""

let app = NSApplication.shared
app.setActivationPolicy(.accessory)

final class D: NSObject, WebFrameLoadDelegate {
    let out: String; let js: String; let w: Int; let h: Int
    init(out: String, js: String, w: Int, h: Int) { self.out = out; self.js = js; self.w = w; self.h = h }
    func webView(_ sender: WebView!, didFinishLoadFor frame: WebFrame!) {
        guard frame == sender.mainFrame else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) {
            if !self.js.isEmpty { sender.stringByEvaluatingJavaScript(from: self.js) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                guard let view = sender.mainFrame.frameView.documentView else { exit(1) }
                view.frame = NSRect(x: 0, y: 0, width: self.w, height: self.h)
                guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
                view.cacheDisplay(in: view.bounds, to: rep)
                guard let png = rep.representation(using: .png, properties: [:]) else { exit(1) }
                try? png.write(to: URL(fileURLWithPath: self.out))
                FileHandle.standardError.write("wrote \(self.out)\n".data(using:.utf8)!)
                exit(0)
            }
        }
    }
    func webView(_ sender: WebView!, didFailLoadWithError error: Error!, for frame: WebFrame!) {
        FileHandle.standardError.write("load fail: \(String(describing: error))\n".data(using:.utf8)!); exit(1)
    }
}

let frame = NSRect(x: 0, y: 0, width: width, height: height)
let web: WebView = WebView(frame: frame, frameName: nil, groupName: nil)
web.preferences.allowsAnimatedImages = true
let d = D(out: outPath, js: js, w: width, h: height)
web.frameLoadDelegate = d
// keep a strong ref
objc_setAssociatedObject(web, "d", d, .OBJC_ASSOCIATION_RETAIN)
web.mainFrame.load(URLRequest(url: URL(string: urlStr)!))

DispatchQueue.main.asyncAfter(deadline: .now() + 25) {
    FileHandle.standardError.write("timeout\n".data(using:.utf8)!); exit(1)
}
app.run()
