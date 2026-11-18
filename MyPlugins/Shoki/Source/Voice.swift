// 書記（Shoki）— 声の特徴と、声のグループ分け（会議のあとの名前付けで使う）
// 外部のモデルは使わない。MFCC（声の音色を表す数字の並び）の平均・ばらつきと、声の高さの統計。
import Foundation
import Accelerate

enum VoiceFeatures {
    static let n = 512
    static let frameLen = 400       // 25 ms
    static let hop = 160            // 10 ms
    static let nMel = 40
    static let nCep = 20
    static let dim = 19 + 19 + 2    // MFCC 平均 19 + ばらつき 19 + 声の高さ 2

    static let window: [Float] = (0..<400).map { 0.54 - 0.46 * cos(2 * Float.pi * Float($0) / 399) }
    static let fftSetup: FFTSetup = vDSP_create_fftsetup(9, FFTRadix(kFFTRadix2))!
    static let mel: [[(Int, Float)]] = makeMel()
    static let dct: [[Float]] = (0..<20).map { k in
        (0..<40).map { m in cos(Float.pi * Float(k) * (Float(m) + 0.5) / 40) }
    }

    static func makeMel() -> [[(Int, Float)]] {
        func hz2mel(_ f: Float) -> Float { 2595 * log10(1 + f / 700) }
        func mel2hz(_ m: Float) -> Float { 700 * (pow(10, m / 2595) - 1) }
        let lo = hz2mel(80), hi = hz2mel(7600)
        let pts = (0..<(nMel + 2)).map { mel2hz(lo + (hi - lo) * Float($0) / Float(nMel + 1)) }
        let binHz = Float(16000) / Float(n)
        var bank: [[(Int, Float)]] = []
        for m in 1...nMel {
            let f0 = pts[m - 1], f1 = pts[m], f2 = pts[m + 1]
            var row: [(Int, Float)] = []
            for k in 0...(n / 2) {
                let f = Float(k) * binHz
                var w: Float = 0
                if f > f0 && f < f1 { w = (f - f0) / (f1 - f0) }
                else if f >= f1 && f < f2 { w = (f2 - f) / (f2 - f1) }
                if w > 0 { row.append((k, w)) }
            }
            bank.append(row)
        }
        return bank
    }

    struct Result { let vector: [Float]; let voiced: Double }

    /// 16 kHz・モノの音から、声の特徴を 1 本の数字の並びにする。声が短すぎれば nil
    static func compute(_ x: [Float]) -> Result? {
        let count = x.count
        guard count >= frameLen * 4 else { return nil }
        let nFrames = (count - frameLen) / hop + 1
        var ceps = [[Float]]()
        ceps.reserveCapacity(nFrames)
        var energy = [Float]()
        energy.reserveCapacity(nFrames)
        var frame = [Float](repeating: 0, count: n)
        var re = [Float](repeating: 0, count: n / 2)
        var im = [Float](repeating: 0, count: n / 2)
        var power = [Float](repeating: 0, count: n / 2 + 1)
        var melE = [Float](repeating: 0, count: nMel)
        for f in 0..<nFrames {
            let off = f * hop
            var e: Float = 0
            for i in 0..<frameLen {
                let cur = x[off + i]
                let prev = off + i > 0 ? x[off + i - 1] : 0
                frame[i] = (cur - 0.97 * prev) * window[i]
                e += cur * cur
            }
            for i in frameLen..<n { frame[i] = 0 }
            energy.append(10 * log10(e / Float(frameLen) + 1e-10))
            re.withUnsafeMutableBufferPointer { rp in
                im.withUnsafeMutableBufferPointer { ip in
                    var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                    frame.withUnsafeBufferPointer { fp in
                        fp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: n / 2) { cp in
                            vDSP_ctoz(cp, 2, &split, 1, vDSP_Length(n / 2))
                        }
                    }
                    vDSP_fft_zrip(fftSetup, &split, 1, 9, FFTDirection(FFT_FORWARD))
                }
            }
            power[0] = re[0] * re[0]
            power[n / 2] = im[0] * im[0]
            for k in 1..<(n / 2) { power[k] = re[k] * re[k] + im[k] * im[k] }
            for m in 0..<nMel {
                var s: Float = 0
                for (k, w) in mel[m] { s += power[k] * w }
                melE[m] = log(s + 1e-8)
            }
            var c = [Float](repeating: 0, count: nCep)
            for k in 0..<nCep {
                var s: Float = 0
                let row = dct[k]
                for m in 0..<nMel { s += row[m] * melE[m] }
                c[k] = s
            }
            ceps.append(c)
        }
        // 声が出ているフレーム: いちばん大きい所から 30 dB 以内、かつ −60 dBFS より上
        let maxE = energy.max() ?? -100
        let thr = max(maxE - 30, -60)
        let idx = (0..<nFrames).filter { energy[$0] > thr }
        guard idx.count >= 30 else { return nil }
        var mean = [Float](repeating: 0, count: nCep)
        for i in idx { for k in 1..<nCep { mean[k] += ceps[i][k] } }
        for k in 1..<nCep { mean[k] /= Float(idx.count) }
        var sd = [Float](repeating: 0, count: nCep)
        for i in idx { for k in 1..<nCep { let d = ceps[i][k] - mean[k]; sd[k] += d * d } }
        for k in 1..<nCep { sd[k] = sqrt(sd[k] / Float(idx.count)) }
        let f0s = pitch(x, frames: idx)
        var v = [Float]()
        v.append(contentsOf: mean[1..<nCep])
        v.append(contentsOf: sd[1..<nCep])
        if f0s.count >= 8 {
            let sorted = f0s.sorted()
            v.append(sorted[sorted.count / 2])
            v.append(sorted[sorted.count * 3 / 4] - sorted[sorted.count / 4])
        } else {
            v.append(.nan); v.append(.nan)
        }
        return Result(vector: v, voiced: Double(idx.count) * Double(hop) / workRate)
    }

    /// 声の高さ（log2 Hz）。正規化した自己相関で 70〜400 Hz を探す（倍の周期を拾わないよう、最初の山を取る）
    static func pitch(_ x: [Float], frames: [Int]) -> [Float] {
        let win = 640
        let minLag = 40, maxLag = 228
        var out = [Float]()
        var nr = [Float](repeating: 0, count: maxLag + 1)
        x.withUnsafeBufferPointer { xp in
            for (j, f) in frames.enumerated() where j % 3 == 0 {
                let start = f * hop
                guard start + win + maxLag <= x.count else { continue }
                let base = xp.baseAddress! + start
                var e0: Float = 0
                vDSP_dotpr(base, 1, base, 1, &e0, vDSP_Length(win))
                guard e0 > 1e-6 else { continue }
                var best: Float = 0
                for lag in minLag...maxLag {
                    var r: Float = 0, el: Float = 0
                    vDSP_dotpr(base, 1, base + lag, 1, &r, vDSP_Length(win))
                    vDSP_dotpr(base + lag, 1, base + lag, 1, &el, vDSP_Length(win))
                    nr[lag] = r / sqrt(e0 * el + 1e-12)
                    best = max(best, nr[lag])
                }
                guard best > 0.6 else { continue }
                var pick = 0
                for lag in (minLag + 1)..<maxLag where nr[lag] >= nr[lag - 1] && nr[lag] >= nr[lag + 1] && nr[lag] > 0.85 * best {
                    pick = lag
                    break
                }
                if pick > 0 { out.append(log2(Float(workRate) / Float(pick))) }
            }
        }
        return out
    }
}

/// 声のグループ分け（Ward 法でまとめてから、k-means で少し直す）
enum VoiceGrouping {
    struct Output {
        var labels: [Int]      // グループ番号（-1 = 声の特徴がない）
        var sure: [Bool]       // そのグループで自信があるか
    }

    static func dist2(_ a: [Float], _ b: [Float]) -> Float {
        var s: Float = 0
        for i in 0..<a.count { let d = a[i] - b[i]; s += d * d }
        return s
    }

    static func group(_ vectors: [[Float]?], voiced: [Double], k: Int) -> Output {
        let n = vectors.count
        var labels = [Int](repeating: -1, count: n)
        var sure = [Bool](repeating: false, count: n)
        let usable = (0..<n).filter { vectors[$0] != nil }
        guard !usable.isEmpty, k >= 1 else { return Output(labels: labels, sure: sure) }
        let dim = VoiceFeatures.dim

        // 1. 次元ごとにそろえる（平均 0・ばらつき 1）。声の高さは重めに
        var mean = [Float](repeating: 0, count: dim)
        var cnt = [Float](repeating: 0, count: dim)
        for i in usable { for d in 0..<dim { let x = vectors[i]![d]; if !x.isNaN { mean[d] += x; cnt[d] += 1 } } }
        for d in 0..<dim { mean[d] = cnt[d] > 0 ? mean[d] / cnt[d] : 0 }
        var sd = [Float](repeating: 0, count: dim)
        for i in usable { for d in 0..<dim { let x = vectors[i]![d]; if !x.isNaN { sd[d] += (x - mean[d]) * (x - mean[d]) } } }
        for d in 0..<dim { sd[d] = cnt[d] > 1 ? max(sqrt(sd[d] / cnt[d]), 1e-4) : 1 }
        let weight: [Float] = (0..<dim).map { d in d >= 38 ? 2.0 : (d < 19 ? 1.0 : 0.7) }
        var z = [[Float]](repeating: [], count: n)
        for i in usable {
            z[i] = (0..<dim).map { d in
                let x = vectors[i]![d]
                return x.isNaN ? 0 : (x - mean[d]) / sd[d] * weight[d]
            }
        }

        // 2. 声が 1 秒以上ある行でグループを作る
        let core = usable.filter { voiced[$0] >= 1.0 }
        let base = core.count >= max(k, 2) ? core : usable
        let kk = min(k, base.count)
        var assign = ward(base.map { z[$0] }, k: kk)

        // 3. k-means で少し直す
        var cents = [[Float]](repeating: [Float](repeating: 0, count: dim), count: kk)
        for _ in 0..<8 {
            var sums = [[Float]](repeating: [Float](repeating: 0, count: dim), count: kk)
            var sizes = [Int](repeating: 0, count: kk)
            for (j, i) in base.enumerated() {
                let c = assign[j]
                sizes[c] += 1
                for d in 0..<dim { sums[c][d] += z[i][d] }
            }
            for c in 0..<kk where sizes[c] > 0 { cents[c] = sums[c].map { $0 / Float(sizes[c]) } }
            var changed = false
            for (j, i) in base.enumerated() {
                var b = assign[j], bd = Float.infinity
                for c in 0..<kk where sizes[c] > 0 {
                    let d = dist2(z[i], cents[c])
                    if d < bd { bd = d; b = c }
                }
                if b != assign[j] { assign[j] = b; changed = true }
            }
            if !changed { break }
        }

        // 4. 全部の行を、いちばん近いグループへ。2 番目との差が小さい・声が短い行は「自信なし」
        for i in usable {
            var d1 = Float.infinity, d2 = Float.infinity, b = 0
            for c in 0..<kk {
                let d = sqrt(dist2(z[i], cents[c]))
                if d < d1 { d2 = d1; d1 = d; b = c } else if d < d2 { d2 = d }
            }
            labels[i] = b
            let margin: Float = kk > 1 ? (d2 - d1) / (d2 + d1 + 1e-6) : 1
            sure[i] = margin > 0.06 && voiced[i] >= 0.6
        }

        // 5. 最初にしゃべった順に A, B, C… となるよう番号を付け直す
        var order = [Int: Int]()
        for i in 0..<n where labels[i] >= 0 && order[labels[i]] == nil { order[labels[i]] = order.count }
        for i in 0..<n where labels[i] >= 0 { labels[i] = order[labels[i]]! }
        return Output(labels: labels, sure: sure)
    }

    /// Ward 法（距離は 2 乗ユークリッド、Lance–Williams の更新式）
    static func ward(_ p: [[Float]], k: Int) -> [Int] {
        let n = p.count
        if n <= k { return Array(0..<n) }
        var D = [[Float]](repeating: [Float](repeating: 0, count: n), count: n)
        for i in 0..<n { for j in (i + 1)..<n { let d = dist2(p[i], p[j]); D[i][j] = d; D[j][i] = d } }
        var size = [Int](repeating: 1, count: n)
        var active = [Bool](repeating: true, count: n)
        var member = Array(0..<n)
        var clusters = n
        while clusters > k {
            var bi = -1, bj = -1
            var best = Float.infinity
            for i in 0..<n where active[i] {
                for j in (i + 1)..<n where active[j] && D[i][j] < best { best = D[i][j]; bi = i; bj = j }
            }
            if bi < 0 { break }
            let ni = Float(size[bi]), nj = Float(size[bj])
            for m in 0..<n where active[m] && m != bi && m != bj {
                let nm = Float(size[m])
                let d = ((ni + nm) * D[bi][m] + (nj + nm) * D[bj][m] - nm * D[bi][bj]) / (ni + nj + nm)
                D[bi][m] = d; D[m][bi] = d
            }
            size[bi] += size[bj]
            active[bj] = false
            for t in 0..<n where member[t] == bj { member[t] = bi }
            clusters -= 1
        }
        var map = [Int: Int]()
        var out = [Int](repeating: 0, count: n)
        for t in 0..<n {
            if map[member[t]] == nil { map[member[t]] = map.count }
            out[t] = map[member[t]]!
        }
        return out
    }
}
