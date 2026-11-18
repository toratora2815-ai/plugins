// 書記（Shoki）のアイコンを描く: swift tools/make_icon.swift <出力.png>
import AppKit
let size: CGFloat = 1024
let img = NSImage(size: NSSize(width: size, height: size))
img.lockFocus()
let ctx = NSGraphicsContext.current!.cgContext
// 背景（角の丸い四角、深い青）
let bg = NSBezierPath(roundedRect: NSRect(x: 100, y: 100, width: 824, height: 824), xRadius: 185, yRadius: 185)
NSGradient(starting: NSColor(calibratedRed: 0.16, green: 0.36, blue: 0.70, alpha: 1),
           ending: NSColor(calibratedRed: 0.09, green: 0.20, blue: 0.45, alpha: 1))!.draw(in: bg, angle: -90)
// 紙
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 30, color: NSColor.black.withAlphaComponent(0.35).cgColor)
NSColor(calibratedWhite: 0.98, alpha: 1).setFill()
NSBezierPath(roundedRect: NSRect(x: 250, y: 190, width: 440, height: 560), xRadius: 36, yRadius: 36).fill()
ctx.setShadow(offset: .zero, blur: 0, color: nil)
// 行（名前の色 + 文）
let rows: [(CGFloat, NSColor, CGFloat)] = [
    (640, NSColor(calibratedRed: 0.20, green: 0.50, blue: 0.95, alpha: 1), 300),
    (560, NSColor(calibratedRed: 0.98, green: 0.58, blue: 0.15, alpha: 1), 250),
    (480, NSColor(calibratedRed: 0.20, green: 0.50, blue: 0.95, alpha: 1), 280),
    (400, NSColor(calibratedRed: 0.98, green: 0.58, blue: 0.15, alpha: 1), 220),
    (320, NSColor(calibratedRed: 0.20, green: 0.50, blue: 0.95, alpha: 1), 260),
]
for (y, c, w) in rows {
    c.setFill()
    NSBezierPath(roundedRect: NSRect(x: 300, y: y, width: 56, height: 34), xRadius: 17, yRadius: 17).fill()
    NSColor(calibratedWhite: 0.72, alpha: 1).setFill()
    NSBezierPath(roundedRect: NSRect(x: 372, y: y + 4, width: w, height: 26), xRadius: 13, yRadius: 13).fill()
}
// 吹き出し（声）
ctx.setShadow(offset: CGSize(width: 0, height: -6), blur: 20, color: NSColor.black.withAlphaComponent(0.3).cgColor)
NSColor(calibratedRed: 0.98, green: 0.58, blue: 0.15, alpha: 1).setFill()
let bubble = NSBezierPath(roundedRect: NSRect(x: 560, y: 600, width: 300, height: 210), xRadius: 105, yRadius: 105)
bubble.fill()
let tail = NSBezierPath()
tail.move(to: NSPoint(x: 620, y: 630)); tail.line(to: NSPoint(x: 570, y: 560)); tail.line(to: NSPoint(x: 680, y: 615)); tail.close(); tail.fill()
ctx.setShadow(offset: .zero, blur: 0, color: nil)
NSColor.white.setFill()
for (i, h) in [CGFloat(60), 110, 80].enumerated() {
    NSBezierPath(roundedRect: NSRect(x: 640 + CGFloat(i) * 52, y: 705 - h / 2, width: 30, height: h), xRadius: 15, yRadius: 15).fill()
}
img.unlockFocus()
let rep = NSBitmapImageRep(data: img.tiffRepresentation!)!
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
