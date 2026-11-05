import Cocoa

let N: CGFloat = 1024
func makeCtx() -> CGContext {
    let c = CGContext(data: nil, width: Int(N), height: Int(N), bitsPerComponent: 8, bytesPerRow: 0,
                      space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    c.translateBy(x: 0, y: N); c.scaleBy(x: 1, y: -1)   // 上が y=0
    return c
}
func save(_ c: CGContext, _ name: String) {
    let rep = NSBitmapImageRep(cgImage: c.makeImage()!)
    try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: name))
}
func col(_ h: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: CGFloat((h >> 16) & 255) / 255, green: CGFloat((h >> 8) & 255) / 255, blue: CGFloat(h & 255) / 255, alpha: a)
}
func grad(_ c: CGContext, _ cols: [CGColor], from: CGPoint, to: CGPoint) {
    let g = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: cols as CFArray, locations: nil)!
    c.drawLinearGradient(g, start: from, end: to, options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
}
func radial(_ c: CGContext, _ cols: [CGColor], _ p: CGPoint, _ r: CGFloat) {
    let g = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: cols as CFArray, locations: nil)!
    c.drawRadialGradient(g, startCenter: p, startRadius: 0, endCenter: p, endRadius: r, options: [])
}
func rr(_ r: CGRect, _ rad: CGFloat) -> CGPath { CGPath(roundedRect: r, cornerWidth: rad, cornerHeight: rad, transform: nil) }

// macOS のアイコンの形（1024 の中に 824 の角丸四角、影つき）
let body = CGRect(x: 100, y: 100, width: 824, height: 824)
func background(_ c: CGContext, top: UInt32, bottom: UInt32) {
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: -12), blur: 28, color: col(0x000000, 0.45))
    c.addPath(rr(body, 185)); c.setFillColor(col(bottom)); c.fillPath()
    c.restoreGState()
    c.saveGState(); c.addPath(rr(body, 185)); c.clip()
    grad(c, [col(top), col(bottom)], from: CGPoint(x: 512, y: 100), to: CGPoint(x: 512, y: 924))
    c.restoreGState()
    // ふちの細い光
    c.addPath(rr(body.insetBy(dx: 2, dy: 2), 183)); c.setStrokeColor(col(0xffffff, 0.10)); c.setLineWidth(4); c.strokePath()
}


// 2026-11 に足したアプリのアイコン（Sound Sketch / Resume Card / Aside / Third Time）
//   swift tools/make-app-icons-2.swift   （MyPlugins/<名前>/Resources/Icon.png に書き出す）
func line(_ c: CGContext, _ pts: [CGPoint], _ color: CGColor, _ w: CGFloat) {
    c.setStrokeColor(color); c.setLineWidth(w); c.setLineCap(.round); c.setLineJoin(.round)
    c.addLines(between: pts); c.strokePath()
}

// Sound Sketch: 口マネの波形 → 虫めがね
do {
    let c = makeCtx()
    background(c, top: 0x1d3b33, bottom: 0x0c1a17)
    let bars: [CGFloat] = [0.25, 0.55, 0.9, 0.65, 0.4, 0.75, 0.5, 0.3]
    for (i, h) in bars.enumerated() {
        let x = 210 + CGFloat(i) * 52, hh = h * 330
        c.addPath(rr(CGRect(x: x, y: 470 - hh / 2, width: 30, height: hh), 15))
        c.setFillColor(col(0x5ee0b0, 0.35 + 0.65 * h)); c.fillPath()
    }
    c.setStrokeColor(col(0xffffff)); c.setLineWidth(46)
    c.strokeEllipse(in: CGRect(x: 520, y: 470, width: 230, height: 230))
    line(c, [CGPoint(x: 720, y: 670), CGPoint(x: 820, y: 770)], col(0xffffff), 58)
    save(c, "MyPlugins/SoundSketch/Resources/Icon.png")
}

// Resume Card: しおりの付いたカード
do {
    let c = makeCtx()
    background(c, top: 0x3a2a14, bottom: 0x16100a)
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: 14), blur: 30, color: col(0x000000, 0.5))
    c.addPath(rr(CGRect(x: 230, y: 250, width: 564, height: 520), 48)); c.setFillColor(col(0xf4efe6)); c.fillPath()
    c.restoreGState()
    for (i, w) in [380.0, 300.0, 340.0].enumerated() {
        c.addPath(rr(CGRect(x: 300, y: 430 + CGFloat(i) * 90, width: CGFloat(w), height: 34), 17))
        c.setFillColor(col(0x8d7f6a, 0.55)); c.fillPath()
    }
    let bm = CGMutablePath()
    bm.move(to: CGPoint(x: 600, y: 210)); bm.addLine(to: CGPoint(x: 720, y: 210)); bm.addLine(to: CGPoint(x: 720, y: 420))
    bm.addLine(to: CGPoint(x: 660, y: 370)); bm.addLine(to: CGPoint(x: 600, y: 420)); bm.closeSubpath()
    c.addPath(bm); c.setFillColor(col(0xffb35c)); c.fillPath()
    save(c, "MyPlugins/ResumeCard/Resources/Icon.png")
}

// Aside: 吹き出し ＋ チェック
do {
    let c = makeCtx()
    background(c, top: 0x2c2350, bottom: 0x110d22)
    let b = CGMutablePath()
    b.addPath(rr(CGRect(x: 220, y: 250, width: 584, height: 420), 120))
    b.move(to: CGPoint(x: 340, y: 640)); b.addLine(to: CGPoint(x: 300, y: 800)); b.addLine(to: CGPoint(x: 470, y: 660)); b.closeSubpath()
    c.addPath(b); c.setFillColor(col(0xb59cff)); c.fillPath()
    line(c, [CGPoint(x: 380, y: 465), CGPoint(x: 470, y: 555), CGPoint(x: 650, y: 375)], col(0x1b1036), 64)
    save(c, "MyPlugins/Aside/Resources/Icon.png")
}

// Third Time: くるっと回る矢印と「3」
do {
    let c = makeCtx()
    background(c, top: 0x3d2410, bottom: 0x170d06)
    c.setStrokeColor(col(0xff9f43)); c.setLineWidth(56); c.setLineCap(.round)
    c.addArc(center: CGPoint(x: 512, y: 512), radius: 270, startAngle: -.pi * 0.35, endAngle: .pi * 1.45, clockwise: false)
    c.strokePath()
    let a = CGMutablePath()
    let tip = CGPoint(x: 512 + 270 * cos(-.pi * 0.35), y: 512 + 270 * sin(-.pi * 0.35))
    a.move(to: CGPoint(x: tip.x + 70, y: tip.y - 40)); a.addLine(to: CGPoint(x: tip.x - 30, y: tip.y - 95)); a.addLine(to: CGPoint(x: tip.x - 20, y: tip.y + 40)); a.closeSubpath()
    c.addPath(a); c.setFillColor(col(0xff9f43)); c.fillPath()
    // 「3」（上下反転した座標なので、文字は一度戻して描く）
    c.saveGState()
    c.translateBy(x: 0, y: N); c.scaleBy(x: 1, y: -1)
    let font = NSFont.systemFont(ofSize: 380, weight: .heavy)
    let s = NSAttributedString(string: "3", attributes: [.font: font, .foregroundColor: NSColor.white])
    let ln = CTLineCreateWithAttributedString(s)
    let bounds = CTLineGetImageBounds(ln, c)
    c.textPosition = CGPoint(x: 512 - bounds.width / 2 - bounds.origin.x, y: 512 - bounds.height / 2 - bounds.origin.y)
    CTLineDraw(ln, c)
    c.restoreGState()
    save(c, "MyPlugins/ThirdTime/Resources/Icon.png")
}
print("ok")
