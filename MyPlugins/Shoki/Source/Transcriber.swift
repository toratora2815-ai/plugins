// 書記（Shoki）— 文字起こし（1 本の音の流れごとに 1 つ）
// macOS 26 からの SpeechAnalyzer / SpeechTranscriber を使う。Mac の中だけで動き、音は外に出ない。
import Foundation
import Speech
import AVFoundation
import CoreMedia
import Accelerate

/// 確定した 1 文
struct Segment {
    var text: String
    var start: Double       // 秒（会議の始まりから）
    var end: Double
    var vector: [Float]?    // 声の特徴（名前付けで使う）
    var voiced: Double      // 声が出ていた長さ（秒）
}

/// 直近の音を覚えておく輪っか（声の特徴を測る・文字起こしが落ちたときに入れ直すため）
final class SampleRing {
    private var buf: [Float]
    private let cap: Int
    private(set) var total: Int64 = 0
    private let lock = NSLock()

    init(seconds: Double) {
        cap = Int(seconds * workRate)
        buf = [Float](repeating: 0, count: cap)
    }

    var oldest: Int64 {
        lock.lock(); defer { lock.unlock() }
        return max(0, total - Int64(cap))
    }

    func append(_ s: [Float]) {
        lock.lock(); defer { lock.unlock() }
        for x in s {
            buf[Int(total % Int64(cap))] = x
            total += 1
        }
    }

    func read(from: Int64, to: Int64) -> [Float] {
        lock.lock(); defer { lock.unlock() }
        let lo = max(from, total - Int64(cap), 0)
        let hi = min(to, total)
        guard hi > lo else { return [] }
        var out = [Float](repeating: 0, count: Int(hi - lo))
        for i in 0..<out.count { out[i] = buf[Int((lo + Int64(i)) % Int64(cap))] }
        return out
    }
}

final class StreamTranscriber {
    let source: String                  // "mic" か "sys"
    // ↓ queue の上でだけ触る
    private var analyzer: SpeechAnalyzer?
    private var cont: AsyncStream<AnalyzerInput>.Continuation?
    private var resultsTask: Task<Void, Never>?
    private var analyzerFormat: AVAudioFormat?
    private var toAnalyzer: AVAudioConverter?
    private var audioFile: AVAudioFile?
    private var fed: Int64 = 0
    private var active = false          // 録っている間 true（解析器が落ちても、音声ファイルと輪っかには書き続ける）
    private var levelSum: Float = 0
    private var levelCount = 0
    private var resyncPending = true    // 次の音で、会議の時計に合わせて無音を詰める（始まりと、つなぎ直しのあと）
    // ↑ ここまで
    private let ring = SampleRing(seconds: 900)
    private let queue: DispatchQueue
    private var startWall = Date()
    private var locale: Locale?
    private var hints: [String] = []
    private let stateLock = NSLock()
    private var stopping = false
    private var lastFinalEnd: Double = 0
    private var restarts = 0
    private var generation = 0
    private var recovering = false
    private var recoverTask: Task<Void, Never>?
    /// テストのときは時計に合わせた無音の詰め物をしない
    var fillGapsByClock = true
    var muted = false
    var onSegment: ((Segment) -> Void)?
    var onVolatile: ((String) -> Void)?
    var onLevel: ((Float) -> Void)?
    /// 文字起こしの具合（"" = 問題なし）
    var onProblem: ((String) -> Void)?

    init(source: String) {
        self.source = source
        queue = DispatchQueue(label: "shoki.stream.\(source)", qos: .userInitiated)
    }

    private var isStopping: Bool {
        stateLock.lock(); defer { stateLock.unlock() }
        return stopping
    }

    func start(hints: [String], audioURL: URL?, startWall: Date) async throws {
        self.startWall = startWall
        self.hints = hints
        let ja = Locale(identifier: "ja_JP")
        guard let loc = await SpeechTranscriber.supportedLocale(equivalentTo: ja) else {
            throw ShokiError.message("この Mac では日本語の文字起こしが使えません")
        }
        locale = loc
        if let url = audioURL {
            let settings: [String: Any] = [
                AVFormatIDKey: kAudioFormatLinearPCM,
                AVSampleRateKey: workRate,
                AVNumberOfChannelsKey: 1,
                AVLinearPCMBitDepthKey: 16,
                AVLinearPCMIsFloatKey: false,
                AVLinearPCMIsBigEndianKey: false
            ]
            let f = try? AVAudioFile(forWriting: url, settings: settings, commonFormat: .pcmFormatFloat32, interleaved: false)
            queue.sync { audioFile = f }
        }
        try await launchAnalyzer(from: 0)
        queue.sync { active = true }
    }

    /// 解析器を作って動かす。fromIndex = この解析器の 0 秒が、流れの何サンプル目か
    private func launchAnalyzer(from fromIndex: Int64) async throws {
        guard let locale else { throw ShokiError.message("文字起こしの言語が決まっていません") }
        let t = SpeechTranscriber(locale: locale, transcriptionOptions: [],
                                  reportingOptions: [.volatileResults],
                                  attributeOptions: [.audioTimeRange])
        if let req = try await AssetInventory.assetInstallationRequest(supporting: [t]) {
            try await req.downloadAndInstall()
        }
        let a = SpeechAnalyzer(modules: [t], options: SpeechAnalyzer.Options(priority: .userInitiated,
                                                                             modelRetention: .processLifetime))
        guard let fmt = await SpeechAnalyzer.bestAvailableAudioFormat(compatibleWith: [t]) else {
            throw ShokiError.message("文字起こしの音の形式が決まりませんでした")
        }
        if !hints.isEmpty {
            let ctx = AnalysisContext()
            ctx.contextualStrings[.general] = hints
            try? await a.setContext(ctx)
        }
        let simple = fmt.sampleRate == workRate && fmt.channelCount == 1 &&
            (fmt.commonFormat == .pcmFormatInt16 || fmt.commonFormat == .pcmFormatFloat32)
        let conv = simple ? nil : AVAudioConverter(from: workFormat, to: fmt)
        let (stream, c) = AsyncStream<AnalyzerInput>.makeStream()
        let offset = Double(fromIndex) / workRate
        let gen: Int = stateLock.withLock { generation += 1; return generation }
        let task = Task { [weak self] in
            var failure: Error?
            do {
                for try await r in t.results { self?.handle(r, offset: offset) }
            } catch {
                failure = error
            }
            self?.resultsEnded(gen: gen, error: failure)
        }
        try await a.start(inputSequence: stream)
        queue.sync {
            analyzer = a
            analyzerFormat = fmt
            toAnalyzer = conv
            resultsTask = task
            // 解析器がいなかった間の音を、輪っかから入れ直す（落ちる直前の、まだ確定していなかった発言も取り戻す）
            if fromIndex < fed {
                let backlog = ring.read(from: fromIndex, to: fed)
                var i = 0
                while i < backlog.count {
                    let n = min(1600, backlog.count - i)
                    let chunk = Array(backlog[i..<(i + n)])
                    if let pcm = makePCM(chunk), let b = analyzerBuffer(pcm, chunk, fmt, conv) { c.yield(AnalyzerInput(buffer: b)) }
                    i += n
                }
            }
            cont = c        // 止める途中でも差し替える（stop() がこの新しい解析器を最後まで仕上げる）
        }
    }

    /// 解析器の結果の流れが終わった（止めた以外なら、落ちたので作り直す）
    private func resultsEnded(gen: Int, error: Error?) {
        // 作り直しは 1 本だけ（作り直し中に古い流れが終わっても、もう 1 本は始めない）
        let go: Bool = stateLock.withLock {
            guard gen == generation, !stopping, !recovering else { return false }
            recovering = true
            return true
        }
        guard go else { return }
        NSLog("Shoki: \(source) の文字起こしが止まった: \(String(describing: error))")
        queue.async { self.cont = nil }     // 落ちた解析器には送らない（音声ファイルと輪っかには書き続ける）
        let task = Task { await self.recover() }
        stateLock.withLock { recoverTask = task }
    }

    private func recover() async {
        defer { stateLock.withLock { recovering = false } }
        while true {
            let n: Int = stateLock.withLock { restarts += 1; return restarts }
            if n > 20 {
                onProblem?("文字起こしが止まりました。「終える」→「記録を始める」で続けてください（ここまでの文字と音声は残っています）")
                return
            }
            onProblem?("文字起こしをつなぎ直しています…")
            // 待つ（止めるときは待たずに、すぐ作り直して最後まで仕上げる）
            let wait = min(5.0, 0.5 * Double(n))
            var waited = 0.0
            while waited < wait && !isStopping {
                try? await Task.sleep(nanoseconds: 100_000_000)
                waited += 0.1
            }
            let from: Double = stateLock.withLock { lastFinalEnd }
            let fromIndex: Int64 = queue.sync { max(Int64(from * workRate), ring.oldest) }
            do {
                try await launchAnalyzer(from: fromIndex)
                onProblem?("")
                return
            } catch {
                NSLog("Shoki: つなぎ直し失敗 \(error)")
                if isStopping { return }
            }
        }
    }

    /// テスト用: 解析器への入り口を閉じて、落ちたのと同じ状態にする
    func debugBreakAnalyzer() {
        let a: SpeechAnalyzer? = queue.sync { analyzer }
        Task { await a?.cancelAndFinishNow() }
    }

    /// 機器をつなぎ直したときに呼ぶ
    func resync() {
        queue.async { self.resyncPending = true }
    }

    /// 音の入り口から呼ばれる（どのスレッドからでもよい）
    func feed(_ s: [Float]) {
        queue.async { self.feedOnQueue(s) }
    }

    private func feedOnQueue(_ s0: [Float]) {
        guard active, !s0.isEmpty else { return }
        let s = muted ? [Float](repeating: 0, count: s0.count) : s0
        if fillGapsByClock {
            let expected = Int64(Date().timeIntervalSince(startWall) * workRate)
            let lag = expected - fed - Int64(s.count)
            var rms: Float = 0
            s.withUnsafeBufferPointer { vDSP_rmsqv($0.baseAddress!, 1, &rms, vDSP_Length(s.count)) }
            if resyncPending {
                // 始まりと、機器のつなぎ直しのあと: 抜けた分を無音で埋めて、会議の時計に合わせる
                resyncPending = false
                if lag > 1600 { push([Float](repeating: 0, count: Int(min(lag, Int64(workRate * 600))))) }
            } else if lag > Int64(workRate) && rms < 0.003 {
                // 見張りが気づかない短い途切れで 1 秒以上遅れたら、静かなときにだけ詰める（話の途中には入れない）
                push([Float](repeating: 0, count: Int(min(lag, Int64(workRate * 600)))))
            }
        }
        push(s)
        for x in s { levelSum += x * x }
        levelCount += s.count
        if levelCount >= 1600 {
            let rms = sqrt(levelSum / Float(levelCount))
            let db = 20 * log10(rms + 1e-9)
            onLevel?(max(0, min(1, (db + 60) / 60)))
            levelSum = 0
            levelCount = 0
        }
    }

    private func makePCM(_ s: [Float]) -> AVAudioPCMBuffer? {
        guard !s.isEmpty, let pcm = AVAudioPCMBuffer(pcmFormat: workFormat, frameCapacity: AVAudioFrameCount(s.count)) else { return nil }
        pcm.frameLength = AVAudioFrameCount(s.count)
        s.withUnsafeBufferPointer { p in pcm.floatChannelData![0].update(from: p.baseAddress!, count: s.count) }
        return pcm
    }

    private func push(_ s: [Float]) {
        guard !s.isEmpty else { return }
        ring.append(s)
        fed += Int64(s.count)
        guard let pcm = makePCM(s) else { return }
        if let f = audioFile { try? f.write(from: pcm) }
        if let c = cont, let fmt = analyzerFormat, let b = analyzerBuffer(pcm, s, fmt, toAnalyzer) {
            c.yield(AnalyzerInput(buffer: b))
        }
    }

    private func analyzerBuffer(_ pcm: AVAudioPCMBuffer, _ s: [Float], _ fmt: AVAudioFormat, _ conv: AVAudioConverter?) -> AVAudioPCMBuffer? {
        if let conv {
            let cap = AVAudioFrameCount(Double(s.count) * fmt.sampleRate / workRate) + 256
            guard let out = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: cap) else { return nil }
            var given = false
            var err: NSError?
            _ = conv.convert(to: out, error: &err) { _, st in
                if given { st.pointee = .noDataNow; return nil }
                given = true; st.pointee = .haveData; return pcm
            }
            return out.frameLength > 0 ? out : nil
        }
        if fmt.commonFormat == .pcmFormatFloat32 { return pcm }
        guard let b = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(s.count)) else { return nil }
        b.frameLength = AVAudioFrameCount(s.count)
        let p = b.int16ChannelData![0]
        for i in 0..<s.count {
            let v = s[i].isFinite ? s[i] : 0
            p[i] = Int16(max(-32768, min(32767, (v * 32767).rounded())))
        }
        return b
    }

    private func handle(_ r: SpeechTranscriber.Result, offset: Double) {
        if !r.isFinal {
            onVolatile?(String(r.text.characters))
            return
        }
        onVolatile?("")
        // 1 つの結果を「文」に分ける（。？！ と、0.9 秒以上の間で区切る）
        var pieces: [(text: String, start: Double, end: Double)] = []
        var cur = ""
        var curStart: Double?
        var curEnd: Double?
        let fallbackStart = r.range.start.seconds
        let fallbackEnd = (r.range.start + r.range.duration).seconds
        func flush() {
            let t = cur.trimmingCharacters(in: .whitespacesAndNewlines)
            if !t.isEmpty {
                let s = curStart ?? fallbackStart
                let e = max(curEnd ?? fallbackEnd, s + 0.2)
                pieces.append((t, s, e))
            }
            cur = ""; curStart = nil; curEnd = nil
        }
        for run in r.text.runs {
            let piece = String(r.text[run.range].characters)
            if let tr = run.audioTimeRange, tr.start.seconds.isFinite, tr.duration.seconds.isFinite {
                let ts = tr.start.seconds
                if let ce = curEnd, ts - ce > 0.9, !cur.trimmingCharacters(in: .whitespaces).isEmpty { flush() }
                if curStart == nil { curStart = ts }
                curEnd = (tr.start + tr.duration).seconds
            }
            cur += piece
            if let last = piece.trimmingCharacters(in: .whitespaces).last, "。？！?!".contains(last) { flush() }
        }
        flush()
        for p in pieces where p.start.isFinite && p.end.isFinite && p.end >= p.start && p.end - p.start < 600 {
            let s = p.start + offset
            let e = p.end + offset
            let samples = ring.read(from: Int64((s - 0.05) * workRate), to: Int64((e + 0.05) * workRate))
            let f = VoiceFeatures.compute(samples)
            stateLock.lock(); lastFinalEnd = max(lastFinalEnd, e); stateLock.unlock()
            onSegment?(Segment(text: p.text, start: s, end: e, vector: f?.vector, voiced: f?.voiced ?? 0))
        }
    }

    /// 止める。最後まで文字にしてから戻る
    func stop() async {
        stateLock.withLock { stopping = true }
        queue.sync { active = false }
        // つなぎ直しの途中なら、終わるのを待つ（新しい解析器で、確定前の発言も最後まで文字にする）
        let rt: Task<Void, Never>? = stateLock.withLock { recoverTask }
        await rt?.value
        let (c, a, t): (AsyncStream<AnalyzerInput>.Continuation?, SpeechAnalyzer?, Task<Void, Never>?) = queue.sync {
            active = false
            return (cont, analyzer, resultsTask)
        }
        c?.finish()
        do { try await a?.finalizeAndFinishThroughEndOfInput() } catch {
            NSLog("Shoki: finalize error \(error)")
        }
        _ = await t?.value
        queue.sync {
            cont = nil
            audioFile = nil      // ファイルを閉じる
        }
    }
}
