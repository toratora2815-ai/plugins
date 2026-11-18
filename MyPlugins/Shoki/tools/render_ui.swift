// 書記（Shoki）の画面を、マウスを使わずに画像に書き出して確かめる（見た目の確認用）
// swiftc -D UITEST -parse-as-library Source/*.swift tools/render_ui.swift -o render_ui && ./render_ui <出力フォルダ> [テストの会議フォルダ]
import SwiftUI
import AppKit

@main
struct RenderUI {
    @MainActor static func main() {
        _ = NSApplication.shared
        NSApp.setActivationPolicy(.prohibited)
        NSApp.appearance = NSAppearance(named: .aqua)
        let out = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")
        let rec = Recorder()
        let started = Date()
        rec.startedAt = started
        rec.recording = true
        rec.elapsed = 754
        rec.micLevel = 0.55
        rec.sysLevel = 0.35
        rec.participants = "緑川、釧路"
        rec.liveSys = "それでは次の議題ですが、来月の"
        rec.message = "記録中。保存先: ホームの「議事録」/ 2026-11-18 2200 会議"
        let sample: [(String, String, Double)] = [
            ("mic", "それでは定例会議を始めます。", 2), ("sys", "よろしくお願いします。先週の売り上げの件ですが、目標の 9割まで来ています。", 5),
            ("sys", "テストの環境がまだ用意できていないので、少し遅れそうです。", 14), ("mic", "では来週の金曜日までにお願いします。", 22),
        ]
        rec.lines = sample.map { LineRecord(source: $0.0, text: $0.1, start: $0.2, end: $0.2 + 3, vector: nil, voiced: 2) }
        render(ContentView().environmentObject(rec), CGSize(width: 1000, height: 640), out.appendingPathComponent("1-記録中.png"))
        rec.recording = false
        rec.lines = []
        rec.elapsed = 0
        rec.micLevel = 0; rec.sysLevel = 0
        rec.message = "「記録を始める」を押すと、自分の声と相手の声を文字にしていきます"
        render(ContentView().environmentObject(rec), CGSize(width: 1000, height: 640), out.appendingPathComponent("0-はじめ.png"))
        if CommandLine.arguments.count > 2, let loaded = Store.load(URL(fileURLWithPath: CommandLine.arguments[2])) {
            let n = Naming(folder: URL(fileURLWithPath: CommandLine.arguments[2]), header: loaded.header, lines: loaded.lines,
                           namesText: "緑川、釧路、佐藤", selfName: "自分")
            if !n.groups.isEmpty { n.groups[0].name = "緑川" }
            render(NamingView(naming: n).environmentObject(rec), CGSize(width: 780, height: 620), out.appendingPathComponent("2-名前を付ける.png"))
        }
        print("OK")
    }

    @MainActor static func render<V: View>(_ v: V, _ size: CGSize, _ url: URL) {
        let host = NSHostingView(rootView: v.background(Color(nsColor: .windowBackgroundColor)))
        host.frame = CGRect(origin: .zero, size: size)
        host.appearance = NSAppearance(named: .aqua)
        host.wantsLayer = true
        let win = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
        win.appearance = NSAppearance(named: .aqua)
        win.contentView = host
        host.layoutSubtreeIfNeeded()
        win.displayIfNeeded()
        RunLoop.current.run(until: Date().addingTimeInterval(0.8))
        let scale: CGFloat = 2
        let w = Int(size.width * scale), h = Int(size.height * scale)
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let layer = host.layer else { return }
        ctx.setFillColor(NSColor.windowBackgroundColor.cgColor)
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.translateBy(x: 0, y: CGFloat(h)); ctx.scaleBy(x: scale, y: -scale)
        if !host.isFlipped { } 
        layer.render(in: ctx)
        guard let img = ctx.makeImage() else { return }
        let rep = NSBitmapImageRep(cgImage: img)
        try? rep.representation(using: .png, properties: [:])?.write(to: url)
    }
}
