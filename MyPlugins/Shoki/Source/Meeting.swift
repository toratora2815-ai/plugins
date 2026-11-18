// 書記（Shoki）— 会議の記録（保存・名前付け・音声の圧縮と削除）
import Foundation
import AVFoundation
import Combine

struct LineRecord: Codable, Identifiable, Equatable {
    var id = UUID()
    var source: String      // "mic"（自分）か "sys"（相手）
    var text: String
    var start: Double       // 会議の始まりからの秒
    var end: Double
    var vector: [Float]?
    var voiced: Double
}

struct MeetingHeader: Codable {
    var started: Date
    var ended: Date?
}

enum JSON {
    static let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.nonConformingFloatEncodingStrategy = .convertToString(positiveInfinity: "inf", negativeInfinity: "-inf", nan: "nan")
        e.dateEncodingStrategy = .iso8601
        return e
    }()
    static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.nonConformingFloatDecodingStrategy = .convertFromString(positiveInfinity: "inf", negativeInfinity: "-inf", nan: "nan")
        d.dateDecodingStrategy = .iso8601
        return d
    }()
}

enum Fmt {
    static func make(_ f: String) -> DateFormatter {
        let d = DateFormatter()
        d.locale = Locale(identifier: "ja_JP")
        d.dateFormat = f
        return d
    }
    static let folder = make("yyyy-MM-dd HHmm")
    static let day = make("yyyy-MM-dd（E）HH:mm")
    static let hm = make("HH:mm")
    static let hms = make("HH:mm:ss")
}

/// 言いよどみ（えー・あのー など）を消す。意味のある言葉は消さない
enum TextClean {
    static let re = try! NSRegularExpression(
        pattern: "(?:^|(?<=[、。？！]))\\s*(?:えー+っと|ええと|えっと|えー+と|えー+|あー+|あのー+|そのー+|うーん|んー+|まあー+)[、,]?\\s*")
    static func clean(_ s: String) -> String {
        let r = re.stringByReplacingMatches(in: s, range: NSRange(s.startIndex..., in: s), withTemplate: "")
        var t = r.trimmingCharacters(in: .whitespaces)
        while t.hasPrefix("、") { t.removeFirst() }
        return t
    }
}

/// スピーカーで聞いていると、相手の声がマイクにも入って「自分」の行として二重に書かれる。
/// マイクの行が、近い時間（前後 4 秒）の相手の行とほぼ同じ文なら「回り込み」とみなして捨てる
enum EchoFilter {
    static func norm(_ t: String) -> [Character] {
        let n = t.precomposedStringWithCompatibilityMapping.applyingTransform(.hiraganaToKatakana, reverse: true) ?? t
        let drop = Set(" 　、。，．,.!?！？「」『』（）()ー〜…・\n\t")
        return n.filter { !drop.contains($0) }.map { $0 }
    }

    static func bigrams(_ c: [Character]) -> [String: Int] {
        var d: [String: Int] = [:]
        if c.count == 1 { d[String(c)] = 1 }
        if c.count >= 2 { for i in 0..<(c.count - 1) { d[String(c[i...(i + 1)]), default: 0] += 1 } }
        return d
    }

    /// a の 2 文字の並びのうち、b にも入っている割合
    static func containment(_ a: [Character], in b: [Character]) -> Double {
        let A = bigrams(a), B = bigrams(b)
        let total = A.values.reduce(0, +)
        guard total > 0 else { return 0 }
        let inter = A.reduce(0) { $0 + min($1.value, B[$1.key] ?? 0) }
        return Double(inter) / Double(total)
    }

    static func isEcho(text: String, start: Double, end: Double, sys: [LineRecord]) -> Bool {
        let near = sys.filter { $0.source == "sys" && $0.start <= end + 4 && $0.end >= start - 4 }
        guard !near.isEmpty else { return false }
        let a = norm(text)
        guard !a.isEmpty else { return false }
        let c = containment(a, in: near.flatMap { norm($0.text) })
        return c >= 0.6 || (c >= 0.5 && a.count <= 10)
    }
}

enum Store {
    static var root: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("議事録", isDirectory: true)
    }
    static let liveName = "記録（時刻つき）.md"
    static let namedName = "議事録（名前つき）.md"
    static let dataName = "data.jsonl"
    static let headerName = "meeting.json"
    static let audioExts: Set<String> = ["caf", "m4a", "wav"]

    static func newMeetingFolder(_ date: Date) throws -> URL {
        let base = Fmt.folder.string(from: date) + " 会議"
        var url = root.appendingPathComponent(base, isDirectory: true)
        var i = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = root.appendingPathComponent(base + " \(i)", isDirectory: true)
            i += 1
        }
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    /// 保存した会議（新しい順）
    static func meetings() -> [URL] {
        let fm = FileManager.default
        guard let items = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil) else { return [] }
        return items.filter { fm.fileExists(atPath: $0.appendingPathComponent(dataName).path) }
            .sorted { $0.lastPathComponent > $1.lastPathComponent }
    }

    static func audioFiles(in folder: URL) -> [URL] {
        let items = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        return items.filter { audioExts.contains($0.pathExtension.lowercased()) }
    }

    /// "自分" か "相手" の音声ファイル（圧縮後の m4a を優先）
    static func audioFile(_ who: String, in folder: URL) -> URL? {
        for ext in ["m4a", "caf", "wav"] {
            let u = folder.appendingPathComponent("\(who).\(ext)")
            if FileManager.default.fileExists(atPath: u.path) { return u }
        }
        return nil
    }

    static func size(_ urls: [URL]) -> Int64 {
        urls.reduce(0) { sum, u in
            let s = (try? FileManager.default.attributesOfItem(atPath: u.path)[.size] as? NSNumber)?.int64Value ?? 0
            return sum + s
        }
    }

    static func sizeText(_ bytes: Int64) -> String {
        if bytes <= 0 { return "なし" }
        let mb = Double(bytes) / 1_000_000
        return mb >= 1000 ? String(format: "%.1f GB", mb / 1000) : String(format: "%.0f MB", max(1, mb))
    }

    /// 音声だけをゴミ箱へ（文字は残る。ゴミ箱から戻せる）
    static func trashAudio(in folder: URL) throws {
        for f in audioFiles(in: folder) { try FileManager.default.trashItem(at: f, resultingItemURL: nil) }
    }

    /// 録音（16 bit の caf）を、小さい m4a（AAC 32 kbps、1 時間で約 14 MB）に変える。うまくいったら caf を消す
    @discardableResult
    static func compress(_ src: URL) -> URL? {
        guard FileManager.default.fileExists(atPath: src.path) else { return nil }
        let final = src.deletingPathExtension().appendingPathExtension("m4a")
        // 仮の名前で書いて、確かめてから本当の名前にする（途中で終わっても、半端な m4a を「聞く」で使わない）
        let dst = src.deletingLastPathComponent().appendingPathComponent(src.deletingPathExtension().lastPathComponent + "-tmp.m4a")
        try? FileManager.default.removeItem(at: dst)
        do {
            let inF = try AVAudioFile(forReading: src)
            guard inF.length > 0 else { return nil }
            var outF: AVAudioFile?
            let base: [String: Any] = [AVFormatIDKey: kAudioFormatMPEG4AAC,
                                       AVSampleRateKey: inF.processingFormat.sampleRate,
                                       AVNumberOfChannelsKey: 1]
            var withRate = base
            withRate[AVEncoderBitRateKey] = 32000
            outF = (try? AVAudioFile(forWriting: dst, settings: withRate, commonFormat: .pcmFormatFloat32, interleaved: false))
            if outF == nil {
                outF = try AVAudioFile(forWriting: dst, settings: base, commonFormat: .pcmFormatFloat32, interleaved: false)
            }
            guard let buf = AVAudioPCMBuffer(pcmFormat: inF.processingFormat, frameCapacity: 160_000) else { return nil }
            while inF.framePosition < inF.length {
                let n = AVAudioFrameCount(min(Int64(buf.frameCapacity), inF.length - inF.framePosition))
                try inF.read(into: buf, frameCount: n)
                try outF?.write(from: buf)
            }
            outF = nil
            let check = try AVAudioFile(forReading: dst)
            let ratio = Double(check.length) / Double(inF.length)
            guard ratio > 0.98 else {
                try? FileManager.default.removeItem(at: dst)
                return nil
            }
            try? FileManager.default.removeItem(at: final)
            try FileManager.default.moveItem(at: dst, to: final)
            try FileManager.default.removeItem(at: src)
            return final
        } catch {
            try? FileManager.default.removeItem(at: dst)
            return nil
        }
    }

    static func load(_ folder: URL) -> (header: MeetingHeader, lines: [LineRecord])? {
        guard let text = try? String(contentsOf: folder.appendingPathComponent(dataName), encoding: .utf8) else { return nil }
        var lines: [LineRecord] = []
        for row in text.split(separator: "\n") {
            if let l = try? JSON.decoder.decode(LineRecord.self, from: Data(row.utf8)) { lines.append(l) }
        }
        var header = MeetingHeader(started: Date(), ended: nil)
        if let d = try? Data(contentsOf: folder.appendingPathComponent(headerName)),
           let h = try? JSON.decoder.decode(MeetingHeader.self, from: d) {
            header = h
        }
        return (header, lines)
    }
}

/// 会議中に 1 行ずつ書き足す（アプリが落ちても、それまでの分は残る）
final class MeetingWriter {
    let folder: URL
    let started: Date
    private var live: FileHandle?
    private var data: FileHandle?

    init(folder: URL, started: Date) throws {
        self.folder = folder
        self.started = started
        let liveURL = folder.appendingPathComponent(Store.liveName)
        let dataURL = folder.appendingPathComponent(Store.dataName)
        let header = """
        # 会議の記録 \(Fmt.day.string(from: started))〜

        - （自分）= マイクの声、（相手）= Mac から出た音（オンライン会議の相手の声）
        - Mac の中だけでの自動の文字起こし。名前は会議のあとに「名前を付ける」で付けます


        """
        try header.write(to: liveURL, atomically: true, encoding: .utf8)
        FileManager.default.createFile(atPath: dataURL.path, contents: nil)
        live = try FileHandle(forWritingTo: liveURL)
        live?.seekToEndOfFile()
        data = try FileHandle(forWritingTo: dataURL)
        writeHeader(MeetingHeader(started: started, ended: nil))
    }

    private func writeHeader(_ h: MeetingHeader) {
        if let d = try? JSON.encoder.encode(h) {
            try? d.write(to: folder.appendingPathComponent(Store.headerName), options: .atomic)
        }
    }

    func append(_ l: LineRecord) {
        let t = Fmt.hms.string(from: started.addingTimeInterval(l.start))
        let who = l.source == "mic" ? "自分" : "相手"
        let row = "[\(t)]（\(who)）\(l.text)\n"
        try? live?.write(contentsOf: Data(row.utf8))
        if var d = try? JSON.encoder.encode(l) {
            d.append(0x0A)
            try? data?.write(contentsOf: d)
        }
        try? live?.synchronize()
        try? data?.synchronize()
    }

    func close(ended: Date) {
        try? live?.write(contentsOf: Data("\n（ここまで。\(Fmt.hm.string(from: ended)) に終了）\n".utf8))
        try? live?.close()
        try? data?.close()
        live = nil
        data = nil
        writeHeader(MeetingHeader(started: started, ended: ended))
    }
}

/// 会議のあとの名前付け
final class Naming: ObservableObject {
    struct Group: Identifiable {
        let id: Int
        var lineIndices: [Int]
        var seconds: Double
        var examples: [Int]
        var nextExample = 0
        var name = ""                   // "" = まだ選んでいない
    }

    let folder: URL
    let lines: [LineRecord]
    let header: MeetingHeader
    @Published var namesText: String
    @Published var groupCount: Int
    @Published var groups: [Group] = []
    @Published var unsureCount = 0
    @Published var playing: Int?
    @Published var note = ""
    @Published var savedURL: URL?
    @Published var saveError = ""
    var selfName: String
    private var labels: [Int] = []
    private var sure: [Bool] = []
    private var player: AVAudioPlayer?
    private var stopWork: DispatchWorkItem?

    static let unknown = "わからない"

    init(folder: URL, header: MeetingHeader, lines: [LineRecord], namesText: String, selfName: String) {
        self.folder = folder
        self.header = header
        self.lines = lines
        self.namesText = namesText
        self.selfName = selfName
        let n = Naming.split(namesText).count
        self.groupCount = max(1, n)
        regroup()
    }

    static func split(_ s: String) -> [String] {
        var seen = Set<String>()
        return s.components(separatedBy: CharacterSet(charactersIn: "、,，\n/／・ 　"))
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty && seen.insert($0).inserted }
    }

    var names: [String] { Naming.split(namesText) }
    var sysCount: Int { lines.filter { $0.source == "sys" }.count }

    func regroup() {
        let sysIdx = lines.indices.filter { lines[$0].source == "sys" }
        labels = [Int](repeating: -1, count: lines.count)
        sure = [Bool](repeating: false, count: lines.count)
        guard !sysIdx.isEmpty else { groups = []; unsureCount = 0; return }
        let out = VoiceGrouping.group(sysIdx.map { lines[$0].vector },
                                      voiced: sysIdx.map { lines[$0].voiced }, k: max(1, groupCount))
        for (j, i) in sysIdx.enumerated() { labels[i] = out.labels[j]; sure[i] = out.sure[j] }
        // 声の特徴が取れなかった短い行は、前後（3 秒以内）が同じグループならそのグループに（自信なしのまま）
        for (j, i) in sysIdx.enumerated() where labels[i] < 0 && j > 0 && j + 1 < sysIdx.count {
            let p = sysIdx[j - 1], q = sysIdx[j + 1]
            if labels[p] >= 0 && labels[p] == labels[q] &&
                lines[i].start - lines[p].end < 3 && lines[q].start - lines[i].end < 3 {
                labels[i] = labels[p]
            }
        }
        let maxLabel = labels.max() ?? -1
        var gs: [Group] = []
        if maxLabel >= 0 {
            for g in 0...maxLabel {
                let idx = lines.indices.filter { labels[$0] == g }
                let secs = idx.reduce(0.0) { $0 + (lines[$1].end - lines[$1].start) }
                let byLength = idx.sorted { (lines[$0].end - lines[$0].start) > (lines[$1].end - lines[$1].start) }
                let sureOnes = byLength.filter { sure[$0] }
                gs.append(Group(id: g, lineIndices: idx, seconds: secs,
                                examples: Array((sureOnes.isEmpty ? byLength : sureOnes).prefix(8))))
            }
        }
        groups = gs
        unsureCount = sysIdx.filter { !sure[$0] }.count
    }

    func exampleTexts(_ g: Group, count: Int = 2) -> [String] {
        g.examples.prefix(count).map { i in
            let t = lines[i].text
            return t.count > 70 ? String(t.prefix(70)) + "…" : t
        }
    }

    func play(_ gi: Int) {
        guard gi < groups.count, !groups[gi].examples.isEmpty else { return }
        guard let url = Store.audioFile("相手", in: folder) else {
            note = "音声ファイルがないので聞けません（消した場合など）。例の文を読んで選んでください"
            return
        }
        let li = groups[gi].examples[groups[gi].nextExample % groups[gi].examples.count]
        groups[gi].nextExample += 1
        let line = lines[li]
        do {
            player?.stop()
            let p = try AVAudioPlayer(contentsOf: url)
            p.currentTime = max(0, line.start - 0.15)
            p.play()
            player = p
            playing = gi
            stopWork?.cancel()
            let w = DispatchWorkItem { [weak self] in
                self?.player?.stop()
                self?.playing = nil
            }
            stopWork = w
            DispatchQueue.main.asyncAfter(deadline: .now() + min(8, line.end - line.start + 0.4), execute: w)
        } catch {
            note = "音声を再生できませんでした: \(error.localizedDescription)"
        }
    }

    func stopPlaying() {
        stopWork?.cancel()
        player?.stop()
        playing = nil
    }

    /// 1 行ごとの名前（"?" は分からない）
    func name(for i: Int) -> String {
        let l = lines[i]
        if l.source == "mic" { return selfName.isEmpty ? "自分" : selfName }
        let g = labels.indices.contains(i) ? labels[i] : -1
        let gname = (g >= 0 && g < groups.count) ? groups[g].name : ""
        if gname.isEmpty || gname == Naming.unknown { return "?" }
        return sure[i] ? gname : gname + "?"
    }

    func render() -> String {
        let started = header.started
        var range = Fmt.day.string(from: started) + "〜"
        if let e = header.ended { range += Fmt.hm.string(from: e) }
        var attendees = [selfName.isEmpty ? "自分" : selfName]
        for g in groups where !g.name.isEmpty && g.name != Naming.unknown && !attendees.contains(g.name) {
            attendees.append(g.name)
        }
        var out = "# 議事録 \(range)\n\n"
        out += "出席: " + attendees.joined(separator: "、") + "\n\n"
        out += "（自動の文字起こしに、声のグループから名前を付けたもの。名前の後ろの ? は自信がない行、（?）はだれか分からない行）\n\n"
        let order = lines.indices.sorted { lines[$0].start < lines[$1].start }
        var curName = ""
        var para = ""
        var lastEnd = -100.0
        func flush() {
            if !para.isEmpty { out += para + "\n\n" }
            para = ""
        }
        let sysLines = lines.filter { $0.source == "sys" }
        for i in order {
            let text = TextClean.clean(lines[i].text)
            if text.isEmpty { continue }
            if lines[i].source == "mic" &&
                EchoFilter.isEcho(text: lines[i].text, start: lines[i].start, end: lines[i].end, sys: sysLines) { continue }
            let nm = name(for: i)
            if nm == curName && lines[i].start - lastEnd < 20 && !para.isEmpty {
                para += text
            } else {
                flush()
                curName = nm
                para = "（\(nm)）" + text
            }
            lastEnd = lines[i].end
        }
        flush()
        return out
    }

    @discardableResult
    func save() throws -> URL {
        let url = folder.appendingPathComponent(Store.namedName)
        try render().write(to: url, atomically: true, encoding: .utf8)
        return url
    }
}
