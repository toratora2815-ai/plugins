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

// ---------- Num Deck：光る 3×3 のキー ----------
do {
    let c = makeCtx()
    background(c, top: 0x2a2f45, bottom: 0x0d0f18)
    let accent: UInt32 = 0xff8a3d
    let gap: CGFloat = 34, key: CGFloat = 186
    let x0 = 512 - (key * 3 + gap * 2) / 2, y0: CGFloat = 512 - (key * 3 + gap * 2) / 2
    for row in 0..<3 { for colI in 0..<3 {
        let r = CGRect(x: x0 + CGFloat(colI) * (key + gap), y: y0 + CGFloat(row) * (key + gap), width: key, height: key)
        let i = row * 3 + colI
        let lit = i == 4
        c.saveGState()
        if lit { c.setShadow(offset: .zero, blur: 60, color: col(accent, 0.9)) }
        else { c.setShadow(offset: CGSize(width: 0, height: 10), blur: 14, color: col(0x000000, 0.6)) }
        c.addPath(rr(r, 40)); c.setFillColor(col(lit ? accent : 0x3a4060)); c.fillPath()
        c.restoreGState()
        c.saveGState(); c.addPath(rr(r, 40)); c.clip()
        grad(c, lit ? [col(0xffb072), col(0xf2652a)] : [col(0x4a5276), col(0x2c3149)],
             from: CGPoint(x: r.midX, y: r.minY), to: CGPoint(x: r.midX, y: r.maxY))
        c.restoreGState()
        c.addPath(rr(r.insetBy(dx: 3, dy: 3), 37)); c.setStrokeColor(col(0xffffff, lit ? 0.35 : 0.12)); c.setLineWidth(5); c.strokePath()

        // 記号：真ん中 = 再生、左上 = 録音、右上 = 巻き戻し…
        let m = CGPoint(x: r.midX, y: r.midY), s: CGFloat = 46
        let glyph = lit ? col(0xffffff) : col(0xc9d0ee, 0.85)
        c.setFillColor(glyph)
        switch i {
        case 4:
            c.move(to: CGPoint(x: m.x - s * 0.75, y: m.y - s)); c.addLine(to: CGPoint(x: m.x + s, y: m.y))
            c.addLine(to: CGPoint(x: m.x - s * 0.75, y: m.y + s)); c.closePath(); c.fillPath()
        case 1:
            c.setFillColor(col(0xff4d5e)); c.fillEllipse(in: CGRect(x: m.x - 38, y: m.y - 38, width: 76, height: 76))
        case 3, 5:
            let d: CGFloat = i == 3 ? -1 : 1
            for k in 0..<2 {
                let ox = m.x + d * (CGFloat(k) * 40 - 20)
                c.move(to: CGPoint(x: ox - d * 30, y: m.y - 30)); c.addLine(to: CGPoint(x: ox + d * 22, y: m.y))
                c.addLine(to: CGPoint(x: ox - d * 30, y: m.y + 30)); c.closePath(); c.fillPath()
            }
        default:
            c.fillEllipse(in: CGRect(x: m.x - 11, y: m.y - 11, width: 22, height: 22))
        }
    }}
    save(c, "NumDeck.png")
}

// ---------- Reference Monitor：スタジオモニター（スピーカー）と波形 ----------
do {
    let c = makeCtx()
    background(c, top: 0x1b2333, bottom: 0x07090f)
    let cyan: UInt32 = 0x38e1ff, pink: UInt32 = 0xff5c8a
    c.addPath(rr(body, 185)); c.clip()   // ここから先はアイコンの形の中だけに描く
    // 後ろの光
    radial(c, [col(cyan, 0.28), col(cyan, 0)], CGPoint(x: 512, y: 560), 420)

    // キャビネット
    let cab = CGRect(x: 300, y: 170, width: 424, height: 600)
    c.saveGState(); c.setShadow(offset: CGSize(width: 0, height: 18), blur: 30, color: col(0x000000, 0.7))
    c.addPath(rr(cab, 56)); c.setFillColor(col(0x1a1d24)); c.fillPath(); c.restoreGState()
    c.saveGState(); c.addPath(rr(cab, 56)); c.clip()
    grad(c, [col(0x3a3f4b), col(0x16181e)], from: CGPoint(x: 300, y: 170), to: CGPoint(x: 724, y: 770))
    c.restoreGState()
    c.addPath(rr(cab.insetBy(dx: 3, dy: 3), 53)); c.setStrokeColor(col(0xffffff, 0.14)); c.setLineWidth(5); c.strokePath()

    func driver(_ p: CGPoint, _ r: CGFloat, glow: UInt32) {
        c.saveGState(); c.setShadow(offset: .zero, blur: 40, color: col(glow, 0.9))
        c.setStrokeColor(col(glow)); c.setLineWidth(r * 0.07)
        c.strokeEllipse(in: CGRect(x: p.x - r, y: p.y - r, width: r * 2, height: r * 2)); c.restoreGState()
        let ri = r * 0.9
        c.saveGState(); c.addEllipse(in: CGRect(x: p.x - ri, y: p.y - ri, width: ri * 2, height: ri * 2)); c.clip()
        radial(c, [col(0x4a5060), col(0x101218)], CGPoint(x: p.x - ri * 0.3, y: p.y - ri * 0.3), ri * 1.4)
        c.restoreGState()
        // コーンの輪
        c.setStrokeColor(col(0xffffff, 0.08)); c.setLineWidth(4)
        for k in [0.72, 0.52] as [CGFloat] {
            let rk = r * k; c.strokeEllipse(in: CGRect(x: p.x - rk, y: p.y - rk, width: rk * 2, height: rk * 2))
        }
        let rc = r * 0.26
        c.saveGState(); c.addEllipse(in: CGRect(x: p.x - rc, y: p.y - rc, width: rc * 2, height: rc * 2)); c.clip()
        radial(c, [col(0x8a93a8), col(0x23262f)], CGPoint(x: p.x - rc * 0.4, y: p.y - rc * 0.4), rc * 1.6)
        c.restoreGState()
    }
    driver(CGPoint(x: 512, y: 300), 68, glow: pink)    // ツイーター
    driver(CGPoint(x: 512, y: 560), 150, glow: cyan)   // ウーファー

    // 下の波形（自分の曲とリファレンスの 2 本）
    func wave(_ color: UInt32, _ phase: CGFloat, _ amp: CGFloat, _ y: CGFloat) {
        c.saveGState(); c.setShadow(offset: .zero, blur: 18, color: col(color, 0.9))
        c.setStrokeColor(col(color)); c.setLineWidth(12); c.setLineCap(.round); c.setLineJoin(.round)
        var first = true
        for xi in stride(from: 190, through: 834, by: 6) {
            let x = CGFloat(xi), t = (x - 190) / 644
            let env = sin(t * .pi)
            let yy = y + amp * env * (sin(t * 22 + phase) * 0.7 + sin(t * 9 + phase * 2) * 0.3)
            if first { c.move(to: CGPoint(x: x, y: yy)); first = false } else { c.addLine(to: CGPoint(x: x, y: yy)) }
        }
        c.strokePath(); c.restoreGState()
    }
    wave(pink, 1.3, 34, 846)
    wave(cyan, 0.0, 46, 846)
    save(c, "ReferenceMonitor.png")
}

// ---------- Hum Keys：声の波が鍵盤（ノートの棒）に変わる ----------
do {
    let c = makeCtx()
    background(c, top: 0x2b2140, bottom: 0x0b0a14)
    let orange: UInt32 = 0xff8a3d, cyan: UInt32 = 0x38e1ff
    c.addPath(rr(body, 185)); c.clip()
    radial(c, [col(orange, 0.22), col(orange, 0)], CGPoint(x: 600, y: 520), 420)

    // 左：声の波（だんだん右へ）
    c.saveGState(); c.setShadow(offset: .zero, blur: 22, color: col(cyan, 0.9))
    c.setStrokeColor(col(cyan)); c.setLineWidth(16); c.setLineCap(.round); c.setLineJoin(.round)
    var first = true
    for xi in stride(from: 170, through: 470, by: 4) {
        let x = CGFloat(xi), t = (x - 170) / 300
        let yy = 512 + 120 * (1 - t * 0.6) * sin(t * 5.5 * .pi) * sin(t * .pi * 0.9 + 0.3)
        if first { c.move(to: CGPoint(x: x, y: yy)); first = false } else { c.addLine(to: CGPoint(x: x, y: yy)) }
    }
    c.strokePath(); c.restoreGState()

    // 右：ノートの棒（ピアノロール）
    let bars: [(CGFloat, CGFloat, CGFloat)] = [(500, 600, 110), (560, 470, 120), (640, 400, 90), (690, 540, 150)]
    for (i, b) in bars.enumerated() {
        let r = CGRect(x: b.0, y: b.1, width: b.2, height: 58)
        c.saveGState(); c.setShadow(offset: .zero, blur: 40, color: col(orange, 0.85))
        c.addPath(rr(r, 18)); c.setFillColor(col(orange)); c.fillPath(); c.restoreGState()
        c.saveGState(); c.addPath(rr(r, 18)); c.clip()
        grad(c, [col(0xffb072), col(i == 3 ? 0xff5c8a : 0xf2652a)], from: CGPoint(x: r.minX, y: r.minY), to: CGPoint(x: r.maxX, y: r.maxY))
        c.restoreGState()
    }
    // 下：鍵盤
    let kx: CGFloat = 190, kw: CGFloat = 92, ky: CGFloat = 760
    for k in 0..<7 {
        let r = CGRect(x: kx + CGFloat(k) * kw, y: ky, width: kw - 8, height: 170)
        c.addPath(rr(r, 14)); c.setFillColor(col(k == 2 ? orange : 0xe8e6f0)); c.fillPath()
    }
    for k in [0, 1, 3, 4, 5] {
        let r = CGRect(x: kx + CGFloat(k) * kw + kw * 0.62, y: ky - 6, width: kw * 0.62, height: 104)
        c.addPath(rr(r, 10)); c.setFillColor(col(0x15131f)); c.fillPath()
    }
    save(c, "HumKeys.png")
}

// ---------- Idea Catcher：ひらめきの電球の中に声の波、右上に録音の赤い点 ----------
do {
    let c = makeCtx()
    background(c, top: 0x2a2416, bottom: 0x0c0b09)
    let amber: UInt32 = 0xffc24b, red: UInt32 = 0xff4d5e
    c.addPath(rr(body, 185)); c.clip()
    radial(c, [col(amber, 0.35), col(amber, 0)], CGPoint(x: 512, y: 440), 430)

    // 電球のガラス（円）と首
    let bulb = CGRect(x: 302, y: 210, width: 420, height: 420)
    c.saveGState(); c.setShadow(offset: .zero, blur: 60, color: col(amber, 0.9))
    c.addEllipse(in: bulb); c.setFillColor(col(amber)); c.fillPath(); c.restoreGState()
    c.saveGState(); c.addEllipse(in: bulb); c.clip()
    grad(c, [col(0xffe39a), col(0xf5a623)], from: CGPoint(x: 512, y: 210), to: CGPoint(x: 512, y: 630))
    c.restoreGState()
    let neck = CGMutablePath()
    neck.move(to: CGPoint(x: 400, y: 590)); neck.addLine(to: CGPoint(x: 624, y: 590))
    neck.addLine(to: CGPoint(x: 600, y: 680)); neck.addLine(to: CGPoint(x: 424, y: 680)); neck.closeSubpath()
    c.addPath(neck); c.setFillColor(col(0xf5a623)); c.fillPath()
    // 口金
    for i in 0..<3 {
        let r = CGRect(x: 420, y: 696 + CGFloat(i) * 46, width: 184, height: 34)
        c.addPath(rr(r, 17)); c.setFillColor(col(i == 2 ? 0x8a8478 : 0xcfc8b8)); c.fillPath()
    }
    // 中の声の波
    c.setStrokeColor(col(0x2a1c05)); c.setLineWidth(20); c.setLineCap(.round); c.setLineJoin(.round)
    var first = true
    for xi in stride(from: 360, through: 664, by: 4) {
        let x = CGFloat(xi), t = (x - 360) / 304
        let yy = 430 + 95 * sin(t * .pi) * sin(t * 4.5 * .pi)
        if first { c.move(to: CGPoint(x: x, y: yy)); first = false } else { c.addLine(to: CGPoint(x: x, y: yy)) }
    }
    c.strokePath()
    // 録音の赤い点
    c.saveGState(); c.setShadow(offset: .zero, blur: 36, color: col(red, 0.9))
    c.addEllipse(in: CGRect(x: 700, y: 196, width: 130, height: 130)); c.setFillColor(col(red)); c.fillPath()
    c.restoreGState()
    save(c, "IdeaCatcher.png")
}
