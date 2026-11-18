// 書記（Shoki）の数値テスト（GUI なし）
// macOS の読み上げ音声で「オンライン会議」を作り、文字起こし → 声のグループ分け → 名前付き議事録 まで通す。
// 使い方: bash build.sh test && build/Shoki/shoki-test <作業フォルダ>
import Foundation
import AVFoundation

struct Utt { let speaker: String; let text: String; var start = 0.0; var end = 0.0 }

func run(_ args: [String]) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/say")
    p.arguments = args
    try? p.run()
    p.waitUntilExit()
}

func readMono16k(_ url: URL) -> [Float] {
    guard let f = try? AVAudioFile(forReading: url) else { return [] }
    let fmt = f.processingFormat
    guard let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(f.length)) else { return [] }
    try? f.read(into: buf)
    let d = Downsampler()
    return d.process(buf)
}

/// 相手ごとのマイク・回線の違いをまねる（2 次のフィルター＋雑音）
struct Biquad {
    var b0: Float, b1: Float, b2: Float, a1: Float, a2: Float
    var z1: Float = 0, z2: Float = 0
    static func lowpass(_ f: Float, q: Float = 0.707) -> Biquad {
        let w = 2 * Float.pi * f / 16000, c = cos(w), s = sin(w), al = s / (2 * q), a0 = 1 + al
        return Biquad(b0: (1 - c) / 2 / a0, b1: (1 - c) / a0, b2: (1 - c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0)
    }
    static func highpass(_ f: Float, q: Float = 0.707) -> Biquad {
        let w = 2 * Float.pi * f / 16000, c = cos(w), s = sin(w), al = s / (2 * q), a0 = 1 + al
        return Biquad(b0: (1 + c) / 2 / a0, b1: -(1 + c) / a0, b2: (1 + c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0)
    }
    mutating func run(_ x: [Float]) -> [Float] {
        x.map { v in
            let y = b0 * v + z1
            z1 = b1 * v - a1 * y + z2
            z2 = b2 * v - a2 * y
            return y
        }
    }
}

@main
struct ShokiTest {
    static func main() async {
        let work = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")
        let voices = work.appendingPathComponent("voices")
        try? FileManager.default.createDirectory(at: voices, withIntermediateDirectories: true)
        var rng = SystemRandomNumberGenerator()

        let remote = [("Kyoko", "緑川"), ("Eddy", "釧路"), ("Grandpa", "佐藤")]
        let selfVoice = "Flo"
        let sentences = [
            "先週の売上の件ですが、目標の九割まで来ています。",
            "はい、その資料は明日までに共有します。",
            "デザインの修正は二回目で確定にしたいと思います。",
            "テストの環境がまだ用意できていないので、少し遅れそうです。",
            "来月のイベントの会場は、駅の近くのホールで決まりました。",
            "予算の上限は五十万円でお願いします。",
            "それについては、もう一度お客さんに確認してみます。",
            "新しいメンバーが来週から入るので、案内をお願いできますか。",
            "スケジュールを一週間後ろにずらしても大丈夫でしょうか。",
            "前回の議事録に書いてあった宿題は、全部終わっています。",
            "アンケートの結果は、来週の会議で発表します。",
            "そうですね、その案でいいと思います。",
        ]
        let selfSentences = [
            "それでは定例会議を始めます。",
            "次の議題に移ります。",
            "では、これで決定ということでお願いします。",
            "次回は来週の水曜日、午後三時からです。",
        ]

        // 1. 読み上げで声を作る
        var utts: [Utt] = []
        for (vi, (voice, name)) in remote.enumerated() {
            for (si, s) in sentences.enumerated() where (si + vi) % 3 != 2 {
                utts.append(Utt(speaker: name, text: s))
                _ = voice
            }
        }
        utts.shuffle(using: &rng)
        func synth(_ voice: String, _ text: String, _ tag: String) -> [Float] {
            let url = voices.appendingPathComponent("\(tag).aiff")
            if !FileManager.default.fileExists(atPath: url.path) {
                run(["-v", voice, "-o", url.path, text])
            }
            return readMono16k(url)
        }
        // 2. 相手側の音の流れ（相手ごとに音色を変える）
        var sys: [Float] = [Float](repeating: 0, count: 16000)
        var filters: [String: (Biquad, Biquad)] = [
            "緑川": (Biquad.lowpass(3400), Biquad.highpass(120)),
            "釧路": (Biquad.lowpass(7000), Biquad.highpass(350)),
            "佐藤": (Biquad.lowpass(5000, q: 1.2), Biquad.highpass(200)),
        ]
        for i in utts.indices {
            let voice = remote.first { $0.1 == utts[i].speaker }!.0
            var x = synth(voice, utts[i].text, "\(voice)-\(sentences.firstIndex(of: utts[i].text)!)")
            var (lp, hp) = filters[utts[i].speaker]!
            x = hp.run(lp.run(x))
            filters[utts[i].speaker] = (lp, hp)
            let g = Float.random(in: 0.5...1.0, using: &rng)
            utts[i].start = Double(sys.count) / 16000
            sys.append(contentsOf: x.map { $0 * g })
            utts[i].end = Double(sys.count) / 16000
            sys.append(contentsOf: [Float](repeating: 0, count: Int(Double.random(in: 0.5...1.3, using: &rng) * 16000)))
        }
        for i in sys.indices { sys[i] += Float.random(in: -0.002...0.002, using: &rng) }
        // 3. 自分側（マイク）は、相手の間に 4 回
        var mic = [Float](repeating: 0, count: sys.count)
        for (k, s) in selfSentences.enumerated() {
            let x = synth(selfVoice, s, "self-\(k)")
            let at = Int(Double(sys.count) * Double(k) / Double(selfSentences.count)) + 8000
            for j in 0..<x.count where at + j < mic.count { mic[at + j] += x[j] * 0.8 }
        }
        // 第 3 引数 echo: スピーカーの音がマイクに回り込む（相手の音を 0.35 倍・30 ms 遅れでマイクに足す）
        let echoMode = CommandLine.arguments.count > 3 && CommandLine.arguments[3] == "echo"
        if echoMode {
            let d = 480
            for i in d..<mic.count { mic[i] += sys[i - d] * 0.35 }
        }
        print(String(format: "音: 相手 %.0f 秒（%d 文）、自分 %d 文", Double(sys.count) / 16000, utts.count, selfSentences.count))

        // 4. 文字起こし（2 本同時）
        let started = Date()
        let folder = work.appendingPathComponent("meeting")
        try? FileManager.default.removeItem(at: folder)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        guard let writer = try? MeetingWriter(folder: folder, started: started) else { print("NG: writer"); return }
        var lines: [LineRecord] = []
        let lock = NSLock()
        let m = StreamTranscriber(source: "mic")
        let s = StreamTranscriber(source: "sys")
        for st in [m, s] {
            st.fillGapsByClock = false
            st.onProblem = { print("problem[\(st.source)]: \($0.isEmpty ? "直った" : $0)") }
            st.onSegment = { seg in
                let l = LineRecord(source: st.source, text: seg.text, start: seg.start, end: seg.end,
                                   vector: seg.vector, voiced: seg.voiced)
                lock.lock(); lines.append(l); lock.unlock()
                writer.append(l)
            }
        }
        let t0 = Date()
        do {
            try await m.start(hints: ["緑川", "釧路"], audioURL: folder.appendingPathComponent("自分.caf"), startWall: started)
            try await s.start(hints: ["緑川", "釧路"], audioURL: folder.appendingPathComponent("相手.caf"), startWall: started)
        } catch { print("NG: start \(error)"); return }
        // 実際の何倍の速さで流すか（0 = できるだけ速く）
        let speed = CommandLine.arguments.count > 2 ? Double(CommandLine.arguments[2]) ?? 0 : 0
        var pos = 0
        while pos < sys.count {
            let n = min(1600, sys.count - pos)
            s.feed(Array(sys[pos..<(pos + n)]))
            m.feed(Array(mic[pos..<(pos + n)]))
            pos += n
            if speed > 0 { try? await Task.sleep(nanoseconds: UInt64(0.1 / speed * 1e9)) }
            // 第 3 引数 crash: 半分のところで相手側の文字起こしをわざと止める（作り直しのテスト）
            if CommandLine.arguments.count > 3 && CommandLine.arguments[3] == "crash" && pos == (sys.count / 2 / 1600) * 1600 {
                print("（相手側の文字起こしをわざと止めた: \(pos / 16000) 秒）")
                s.debugBreakAnalyzer()
            }
        }
        await m.stop()
        await s.stop()
        writer.close(ended: Date())
        print(String(format: "文字起こし: %.1f 秒で終了、%d 行", Date().timeIntervalSince(t0), lines.count))

        // 5. 正解と照らす
        lines.sort { $0.start < $1.start }
        func truth(_ l: LineRecord) -> String {
            if l.source == "mic" { return "自分" }
            let mid = (l.start + l.end) / 2
            return utts.min { abs(($0.start + $0.end) / 2 - mid) < abs(($1.start + $1.end) / 2 - mid) }!.speaker
        }
        let sysLines = lines.filter { $0.source == "sys" }
        if echoMode {
            let micLines = lines.filter { $0.source == "mic" }
            let echoes = micLines.filter { EchoFilter.isEcho(text: $0.text, start: $0.start, end: $0.end, sys: sysLines) }
            let selfKept = micLines.filter { m in !echoes.contains(m) && selfSentences.contains { s in EchoFilter.containment(EchoFilter.norm(s), in: EchoFilter.norm(m.text)) > 0.5 } }
            print("回り込み: 自分の行 \(micLines.count) のうち \(echoes.count) 行を捨てる、本人の文で残ったもの \(selfKept.count)/\(selfSentences.count)")
            for m in micLines where !echoes.contains(m) { print("  残った自分の行: \(m.text)") }
        }
        let withVec = sysLines.filter { $0.vector != nil }.count
        print("相手の行: \(sysLines.count)（声の特徴あり \(withVec)）、自分の行: \(lines.count - sysLines.count)")

        // 6. 名前付け（人が「▶ 聞く」で正しい名前を選んだことにする）
        let naming = Naming(folder: folder, header: MeetingHeader(started: started, ended: Date()),
                            lines: lines, namesText: "緑川、釧路、佐藤", selfName: "自分")
        for gi in naming.groups.indices {
            var count: [String: Int] = [:]
            for i in naming.groups[gi].lineIndices { count[truth(lines[i]), default: 0] += 1 }
            naming.groups[gi].name = count.max { $0.value < $1.value }?.key ?? ""
            print("グループ \(gi): \(naming.groups[gi].lineIndices.count) 行 → \(naming.groups[gi].name)  内訳 \(count)")
        }
        var right = 0, wrong = 0, unsureRight = 0, unsureWrong = 0, unknown = 0
        for i in lines.indices where lines[i].source == "sys" {
            let n = naming.name(for: i)
            if n == "?" { unknown += 1; continue }
            let isUnsure = n.hasSuffix("?")
            let base = isUnsure ? String(n.dropLast()) : n
            let ok = base == truth(lines[i])
            if isUnsure { if ok { unsureRight += 1 } else { unsureWrong += 1 } }
            else { if ok { right += 1 } else { wrong += 1 } }
        }
        print("名前: 自信あり 正 \(right) / 誤 \(wrong)、自信なし 正 \(unsureRight) / 誤 \(unsureWrong)、不明 \(unknown)")
        if let url = try? naming.save() {
            print("--- \(url.lastPathComponent)")
            print((try? String(contentsOf: url, encoding: .utf8)) ?? "")
        }
        // 7. 音声の圧縮
        for who in ["自分", "相手"] {
            let src = folder.appendingPathComponent("\(who).caf")
            let before = Store.size([src])
            if let dst = Store.compress(src) {
                print("圧縮 \(who): \(Store.sizeText(before)) → \(Store.sizeText(Store.size([dst])))（\(dst.lastPathComponent)）")
            } else {
                print("NG: 圧縮できなかった \(who)")
            }
        }
        print("TEXTCLEAN: " + TextClean.clean("えーと、それでは、あのー始めます。えー、次です。まあいいか。"))
    }
}
