// 書記（Shoki）— 画面
import SwiftUI
import AppKit
import AVFoundation

@MainActor
final class Recorder: ObservableObject {
    @Published var recording = false
    @Published var busy = false
    @Published var lines: [LineRecord] = []
    @Published var liveMic = ""
    @Published var liveSys = ""
    @Published var micLevel: Float = 0
    @Published var sysLevel: Float = 0
    @Published var message = "「記録を始める」を押すと、自分の声と相手の声を文字にしていきます"
    @Published var elapsed = 0
    @Published var startedAt = Date()
    @Published var micPaused = false { didSet { micStream?.muted = micPaused } }
    @Published var inputs: [AudioInputDevice] = []
    @Published var inputUID: String { didSet { UserDefaults.standard.set(inputUID, forKey: "inputUID") } }
    @Published var selfName: String { didSet { UserDefaults.standard.set(selfName, forKey: "selfName") } }
    @Published var participants: String { didSet { UserDefaults.standard.set(participants, forKey: "participants") } }
    @Published var hints: String { didSet { UserDefaults.standard.set(hints, forKey: "hints") } }
    @Published var folder: URL?
    @Published var naming: Naming?
    @Published var showNaming = false
    @Published var showMeetings = false
    @Published var sysQuiet = false
    @Published var confirmDelete = false
    @Published var problem = ""             // 赤で出す（文字起こしが止まった など）

    private var stopTask: Task<Void, Never>?
    private var lastMicLevelAt = Date()
    private var lastSysLevelAt = Date()
    private var sysMissing = false
    private var pendingMic: [(seg: Segment, arrived: Date)] = []
    @Published var echoCount = 0            // スピーカーから回り込んで捨てた自分の行の数
    private var mic: MicCapture?
    private var sys: SystemCapture?
    private var micStream: StreamTranscriber?
    private var sysStream: StreamTranscriber?
    private var writer: MeetingWriter?
    private var timer: Timer?
    private var activity: NSObjectProtocol?
    private var lastSysSound = Date()

    init() {
        let d = UserDefaults.standard
        inputUID = d.string(forKey: "inputUID") ?? ""
        selfName = d.string(forKey: "selfName") ?? "自分"
        participants = d.string(forKey: "participants") ?? ""
        hints = d.string(forKey: "hints") ?? ""
        refreshInputs()
    }

    var sortedLines: [LineRecord] { lines.sorted { $0.start < $1.start } }

    func refreshInputs() { inputs = AudioDevices.inputDevices() }

    func toggle() {
        Task {
            if recording { await stop() } else { await start() }
        }
    }

    func start() async {
        guard !recording, !busy else { return }
        busy = true
        message = "準備中…"
        guard await AVCaptureDevice.requestAccess(for: .audio) else {
            message = "マイクの許可がありません。システム設定 → プライバシーとセキュリティ → マイク で「書記」を ON にしてください"
            busy = false
            return
        }
        let started = Date()
        let f: URL
        do {
            f = try Store.newMeetingFolder(started)
            writer = try MeetingWriter(folder: f, started: started)
        } catch {
            message = "保存先を作れませんでした: \(error.localizedDescription)"
            busy = false
            return
        }
        folder = f
        startedAt = started
        lines = []
        pendingMic = []
        echoCount = 0
        liveMic = ""
        liveSys = ""
        let hintList = Naming.split(participants) + Naming.split(hints)
        let m = StreamTranscriber(source: "mic")
        let s = StreamTranscriber(source: "sys")
        m.muted = micPaused
        for st in [m, s] {
            let src = st.source
            st.onSegment = { [weak self] seg in
                Task { @MainActor in self?.add(seg, source: src) }
            }
            st.onVolatile = { [weak self] t in
                Task { @MainActor in
                    if src == "mic" { self?.liveMic = t } else { self?.liveSys = t }
                }
            }
            st.onLevel = { [weak self] v in
                Task { @MainActor in
                    guard let self else { return }
                    if src == "mic" { self.micLevel = v; self.lastMicLevelAt = Date() }
                    else { self.sysLevel = v; self.lastSysLevelAt = Date() }
                }
            }
            st.onProblem = { [weak self] text in
                Task { @MainActor in
                    let who = src == "mic" ? "自分" : "相手"
                    self?.problem = text.isEmpty ? "" : "（\(who)）" + text
                }
            }
        }
        problem = ""
        do {
            try await m.start(hints: hintList, audioURL: f.appendingPathComponent("自分.caf"), startWall: started)
            try await s.start(hints: hintList, audioURL: f.appendingPathComponent("相手.caf"), startWall: started)
        } catch {
            message = "文字起こしを始められませんでした: \(error.localizedDescription)"
            writer?.close(ended: Date())
            writer = nil
            busy = false
            return
        }
        micStream = m
        sysStream = s
        var problems: [String] = []
        let mc = MicCapture()
        mc.deviceUID = inputUID.isEmpty ? nil : inputUID
        mc.onSamples = { [weak m] x in m?.feed(x) }
        mc.onRestart = { [weak m] in m?.resync() }
        do { try mc.start() } catch { problems.append("マイク: \(error.localizedDescription)") }
        let sc = SystemCapture()
        sc.onSamples = { [weak s] x in s?.feed(x) }
        sc.onRestart = { [weak s] in s?.resync() }
        do { try sc.start() } catch { problems.append("Mac の音: \(error.localizedDescription)") }
        mic = mc
        sys = sc
        activity = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .idleSystemSleepDisabled, .latencyCritical],
                                                         reason: "会議を記録中")
        recording = true
        busy = false
        elapsed = 0
        lastSysSound = Date()
        lastMicLevelAt = Date()
        lastSysLevelAt = Date()
        sysQuiet = false
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
        message = problems.isEmpty ? "記録中。保存先: ホームの「議事録」/ \(f.lastPathComponent)"
                                   : "記録中（うまくいかない所: " + problems.joined(separator: " / ") + "）"
    }

    private func tick() {
        guard recording else { return }
        flushMic(force: false)
        let now = Date()
        elapsed = Int(now.timeIntervalSince(startedAt))
        // メーターは 1 秒来なければ 0 に戻す
        if now.timeIntervalSince(lastMicLevelAt) > 1 { micLevel = 0 }
        if now.timeIntervalSince(lastSysLevelAt) > 1 { sysLevel = 0 }
        // マイクの音が 3 秒来なければ、つなぎ直す
        if now.timeIntervalSince(lastMicLevelAt) > 3 {
            mic?.restart()
            lastMicLevelAt = now.addingTimeInterval(5)     // つなぎ直しに時間がかかる機器（AirPods など）のため、次は 8 秒待つ
            message = "マイクをつなぎ直しました（\(Fmt.hms.string(from: now))）"
        }
        // 相手の音が 5 秒来なければ知らせる（Mac の音の見張りが、裏でつなぎ直しを続けている）。戻ったら消す
        if now.timeIntervalSince(lastSysLevelAt) > 5 {
            sysMissing = true
            message = "相手（Mac の音）が届いていません。出力（ヘッドホン）を確かめてください。直らなければ「終える」→「記録を始める」"
        } else if sysMissing {
            sysMissing = false
            message = "相手（Mac の音）が戻りました（\(Fmt.hms.string(from: now))）。記録中"
        }
        if sysLevel > 0.05 { lastSysSound = now }
        sysQuiet = now.timeIntervalSince(lastSysSound) > 30
    }

    private func add(_ seg: Segment, source: String) {
        if source == "mic" {
            // 自分の行は 4 秒待ってから（相手の行と比べて、スピーカーからの回り込みなら捨てる）
            pendingMic.append((seg, Date()))
            return
        }
        commit(seg, source: source)
    }

    private func commit(_ seg: Segment, source: String) {
        let l = LineRecord(source: source, text: seg.text, start: seg.start, end: seg.end,
                           vector: seg.vector, voiced: seg.voiced)
        lines.append(l)
        writer?.append(l)
    }

    private func flushMic(force: Bool) {
        let now = Date()
        var keep: [(seg: Segment, arrived: Date)] = []
        for p in pendingMic {
            if !force && now.timeIntervalSince(p.arrived) < 4 { keep.append(p); continue }
            if EchoFilter.isEcho(text: p.seg.text, start: p.seg.start, end: p.seg.end, sys: lines) {
                echoCount += 1
            } else {
                commit(p.seg, source: "mic")
            }
        }
        pendingMic = keep
    }

    /// 止める（2 回呼ばれても、終了のときも、同じ仕上げを待つ）
    func stop() async {
        if let t = stopTask { await t.value; return }
        guard recording else { return }
        let t = Task { await self.finish() }
        stopTask = t
        await t.value
        stopTask = nil
    }

    /// アプリを終えるとき: 記録中・仕上げ中なら、最後まで仕上げてから
    func finishForQuit() async {
        if let t = stopTask { await t.value } else if recording { await stop() }
    }

    private func finish() async {
        busy = true
        message = "仕上げ中…（最後の発言まで文字にしています）"
        timer?.invalidate()
        timer = nil
        mic?.stop()
        sys?.stop()
        mic = nil
        sys = nil
        await micStream?.stop()
        await sysStream?.stop()
        micStream = nil
        sysStream = nil
        try? await Task.sleep(nanoseconds: 400_000_000)
        flushMic(force: true)
        writer?.close(ended: Date())
        writer = nil
        if let a = activity { ProcessInfo.processInfo.endActivity(a); activity = nil }
        recording = false
        liveMic = ""
        liveSys = ""
        micLevel = 0
        sysLevel = 0
        sysQuiet = false
        problem = ""
        message = "音声を小さくしています…"
        if let f = folder {
            await Task.detached {
                for who in ["自分", "相手"] { Store.compress(f.appendingPathComponent("\(who).caf")) }
            }.value
        }
        busy = false
        message = "保存しました。次は「名前を付ける」で、相手の声に名前を付けます"
        openNaming()
    }

    func openNaming(folder f0: URL? = nil) {
        guard let f = f0 ?? folder, let loaded = Store.load(f) else {
            message = "会議の記録が見つかりません"
            return
        }
        if f0 != nil {
            folder = f
            lines = loaded.lines
            startedAt = loaded.header.started
        }
        naming = Naming(folder: f, header: loaded.header, lines: loaded.lines,
                        namesText: participants, selfName: selfName)
        showNaming = true
    }

    func named(_ url: URL) {
        message = "保存しました: \(url.lastPathComponent)（Claude に「清書して」と頼むと、議事録の書き方にそろえます）"
    }

    func openFolder(_ f: URL? = nil) {
        try? FileManager.default.createDirectory(at: Store.root, withIntermediateDirectories: true)
        NSWorkspace.shared.open(f ?? folder ?? Store.root)
    }

    func trashAudio(of f: URL?) {
        guard let f else { return }
        do {
            let size = Store.size(Store.audioFiles(in: f))
            try Store.trashAudio(in: f)
            message = "音声（\(Store.sizeText(size))）をゴミ箱に入れました。文字は残っています"
        } catch {
            message = "音声を消せませんでした: \(error.localizedDescription)"
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var rec: Recorder?

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let rec, rec.recording || rec.busy else { return .terminateNow }
        Task { @MainActor in
            await rec.finishForQuit()
            NSApp.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }
}

#if !UITEST
@main
struct ShokiApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var rec = Recorder()

    var body: some Scene {
        Window("書記", id: "main") {
            ContentView()
                .environmentObject(rec)
                .frame(minWidth: 860, minHeight: 560)
                .onAppear { delegate.rec = rec }
        }
        .defaultSize(width: 1000, height: 720)
    }
}
#endif

func timeText(_ s: Int) -> String {
    String(format: "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
}

func groupLetter(_ i: Int) -> String {
    let letters = Array("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    return i < letters.count ? String(letters[i]) : "\(i + 1)"
}

struct ContentView: View {
    @EnvironmentObject var rec: Recorder

    var body: some View {
        VStack(spacing: 0) {
            TopBar()
            Divider()
            TranscriptView()
            Divider()
            BottomBar()
        }
        .sheet(isPresented: $rec.showNaming) {
            if let n = rec.naming { NamingView(naming: n).environmentObject(rec) }
        }
        .sheet(isPresented: $rec.showMeetings) {
            MeetingsView().environmentObject(rec)
        }
    }
}

struct Meter: View {
    let label: String
    let value: Float
    let paused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(paused ? label + "（一時停止中）" : label).font(.caption).foregroundStyle(.secondary)
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 3).fill(Color.secondary.opacity(0.2))
                RoundedRectangle(cornerRadius: 3).fill(paused ? Color.gray : Color.green)
                    .frame(width: 150 * CGFloat(paused ? 0 : value))
            }
            .frame(width: 150, height: 8)
        }
    }
}

struct TopBar: View {
    @EnvironmentObject var rec: Recorder

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 16) {
                Button(action: { rec.toggle() }) {
                    Label(rec.recording ? "終える" : "記録を始める",
                          systemImage: rec.recording ? "stop.fill" : "record.circle")
                        .font(.title3.bold())
                        .frame(minWidth: 150)
                        .padding(.vertical, 6)
                }
                .buttonStyle(.borderedProminent)
                .tint(rec.recording ? .red : .accentColor)
                .disabled(rec.busy)
                Text(timeText(rec.elapsed))
                    .font(.title2.monospacedDigit())
                    .foregroundStyle(rec.recording ? .primary : .secondary)
                if rec.busy { ProgressView().controlSize(.small) }
                Spacer()
                Meter(label: "自分（マイク）", value: rec.micLevel, paused: rec.micPaused)
                Meter(label: "相手（Mac の音）", value: rec.sysLevel, paused: false)
            }
            if !rec.problem.isEmpty {
                Text(rec.problem)
                    .font(.callout.bold())
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if rec.recording && rec.echoCount >= 3 {
                Text("スピーカーの音がマイクに入っています（相手の声が「自分」として二重にならないよう、自動で \(rec.echoCount) 行消しました）。ヘッドホンを使うと、より確実です")
                    .font(.callout)
                    .foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if rec.recording && rec.sysQuiet {
                Text("相手（Mac の音）が 30 秒以上鳴っていません。相手の声が入らないときは: システム設定 → プライバシーとセキュリティ → 画面とシステムオーディオ録音 → 下の「システムオーディオ録音のみ」で「書記」を ON（鳴っていないだけなら、そのままで大丈夫）")
                    .font(.callout)
                    .foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 12) {
                Picker("マイク", selection: $rec.inputUID) {
                    Text("標準のマイク").tag("")
                    ForEach(rec.inputs) { d in Text(d.name).tag(d.uid) }
                }
                .frame(maxWidth: 320)
                .disabled(rec.recording)
                Button { rec.refreshInputs() } label: { Image(systemName: "arrow.clockwise") }
                    .help("マイクの一覧を読み直す")
                    .disabled(rec.recording)
                Toggle("自分のマイクを一時停止", isOn: $rec.micPaused)
                    .toggleStyle(.checkbox)
                    .help("会議でミュートしている間のひとりごとなどを、文字にしたくないとき")
                Spacer()
                Text("自分の名前").foregroundStyle(.secondary)
                TextField("自分", text: $rec.selfName).frame(width: 120)
            }
            HStack(spacing: 8) {
                Text("参加者（相手）").foregroundStyle(.secondary)
                TextField("例: 緑川、釧路（名前付けと、聞き取りのヒントに使います）", text: $rec.participants)
                Text("言葉のヒント").foregroundStyle(.secondary)
                TextField("例: 案件名、製品名", text: $rec.hints).frame(maxWidth: 260)
            }
            .disabled(rec.recording)
        }
        .padding(14)
    }
}

struct LineRow: View {
    let line: LineRecord
    let started: Date

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Text(Fmt.hms.string(from: started.addingTimeInterval(line.start)))
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
            Text(line.source == "mic" ? "（自分）" : "（相手）")
                .bold()
                .foregroundStyle(line.source == "mic" ? Color.blue : Color.orange)
            Text(line.text)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

struct TranscriptView: View {
    @EnvironmentObject var rec: Recorder

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 6) {
                    if rec.lines.isEmpty && !rec.recording {
                        HelpText()
                    }
                    ForEach(rec.sortedLines) { l in
                        LineRow(line: l, started: rec.startedAt)
                    }
                    if rec.recording {
                        if !rec.liveSys.isEmpty {
                            Text("（相手・話し中）" + rec.liveSys).foregroundStyle(.secondary)
                        }
                        if !rec.liveMic.isEmpty {
                            Text("（自分・話し中）" + rec.liveMic).foregroundStyle(.secondary)
                        }
                    }
                    Color.clear.frame(height: 1).id("bottom")
                }
                .padding(14)
                .textSelection(.enabled)
            }
            .onChange(of: rec.lines.count) { _, _ in
                withAnimation { proxy.scrollTo("bottom", anchor: .bottom) }
            }
        }
    }
}

struct HelpText: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("使い方").font(.headline)
            Text("1. Teams・Zoom などで会議に入る（ヘッドホンを使う。スピーカーだと相手の声が「自分」としても入ります）")
            Text("2. 上の「参加者（相手）」に名前を入れる（例: 緑川、釧路）")
            Text("3. 「記録を始める」を押す。会議の最初に、記録していることを一言伝える")
            Text("4. 終わったら「終える」→「名前を付ける」で、相手の声のグループごとに ▶ で聞いて、だれかを選ぶ")
            Text("5. できた「議事録（名前つき）.md」を Claude に渡して「清書して」と頼むと、いつもの議事録の書き方にそろいます")
            Text("保存先: ホームの「議事録」フォルダ（iCloud には上げません）。音声は「保存した会議…」からいつでも消せます")
                .foregroundStyle(.secondary)
        }
        .padding(.bottom, 12)
    }
}

struct BottomBar: View {
    @EnvironmentObject var rec: Recorder

    var body: some View {
        HStack(spacing: 10) {
            Text(rec.message)
                .font(.callout)
                .foregroundStyle(.secondary)
                .lineLimit(2)
            Spacer()
            Button("フォルダを開く") { rec.openFolder() }
            Button("名前を付ける…") { rec.openNaming() }
                .disabled(rec.recording || rec.busy || rec.folder == nil)
            Button("この会議の音声を消す…") { rec.confirmDelete = true }
                .disabled(rec.recording || rec.busy || rec.folder == nil)
            Button("保存した会議…") { rec.showMeetings = true }
                .disabled(rec.recording || rec.busy)
        }
        .padding(12)
        .confirmationDialog("この会議の音声ファイルをゴミ箱に入れますか？", isPresented: $rec.confirmDelete) {
            Button("音声をゴミ箱に入れる", role: .destructive) { rec.trashAudio(of: rec.folder) }
            Button("やめる", role: .cancel) {}
        } message: {
            Text("文字（記録・議事録）は残ります。ゴミ箱を空にするまでは戻せます。")
        }
    }
}

struct MeetingItem: Identifiable {
    let id: URL
    let audio: Int64
    let named: Bool
}

final class MeetingsModel: ObservableObject {
    @Published var items: [MeetingItem] = []
    @Published var confirmAll = false

    func reload() {
        items = Store.meetings().map { u in
            MeetingItem(id: u, audio: Store.size(Store.audioFiles(in: u)),
                        named: FileManager.default.fileExists(atPath: u.appendingPathComponent(Store.namedName).path))
        }
    }
}

struct MeetingsView: View {
    @EnvironmentObject var rec: Recorder
    @Environment(\.dismiss) private var dismiss
    @StateObject private var model = MeetingsModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("保存した会議").font(.title2.bold())
            Text("「音声を消す」は音声ファイルだけをゴミ箱に入れます。文字（記録・議事録）は残ります。")
                .foregroundStyle(.secondary)
            List {
                ForEach(model.items) { it in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(it.id.lastPathComponent)
                            Text("音声 " + Store.sizeText(it.audio) + (it.named ? "・名前付け済み" : ""))
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        Button("名前を付ける…") {
                            let u = it.id
                            dismiss()
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { rec.openNaming(folder: u) }
                        }
                        Button("開く") { rec.openFolder(it.id) }
                        Button("音声を消す") {
                            rec.trashAudio(of: it.id)
                            model.reload()
                        }
                        .disabled(it.audio == 0)
                    }
                    .padding(.vertical, 2)
                }
            }
            HStack {
                Text("音声の合計: " + Store.sizeText(model.items.reduce(0) { $0 + $1.audio }))
                Spacer()
                Button("音声を全部消す…") { model.confirmAll = true }
                    .disabled(model.items.allSatisfy { $0.audio == 0 })
                Button("閉じる") { dismiss() }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 760, height: 480)
        .onAppear { model.reload() }
        .confirmationDialog("すべての会議の音声ファイルをゴミ箱に入れますか？", isPresented: $model.confirmAll) {
            Button("音声を全部ゴミ箱に入れる", role: .destructive) {
                for it in model.items where it.audio > 0 { rec.trashAudio(of: it.id) }
                model.reload()
            }
            Button("やめる", role: .cancel) {}
        } message: {
            Text("文字（記録・議事録）は残ります。ゴミ箱を空にするまでは戻せます。")
        }
    }
}

struct NamingView: View {
    @ObservedObject var naming: Naming
    @EnvironmentObject var rec: Recorder
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("名前を付ける").font(.title2.bold())
            Text("相手（Mac の音）の声を、似ている声どうしでグループに分けました。グループごとに「聞く」で確かめて（押すたびに別の発言）、だれの声かを選んでください。自分（マイク）の行には「\(naming.selfName)」が付きます。")
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Text("相手の名前")
                TextField("例: 緑川、釧路", text: $naming.namesText)
                Stepper("グループの数: \(naming.groupCount)", value: $naming.groupCount, in: 1...12)
                Button("分け直す") { naming.regroup() }
            }
            if naming.sysCount == 0 {
                Text("相手（Mac の音）の行がありません。自分の行だけで保存できます。").foregroundStyle(.secondary)
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(naming.groups) { g in
                        GroupRow(naming: naming, group: g)
                    }
                }
            }
            if !naming.note.isEmpty { Text(naming.note).foregroundStyle(.orange) }
            Text("自信がない行: \(naming.unsureCount) 行（名前の後ろに ? が付きます）。名前を選ばなかったグループの行は（?）になります。")
                .font(.callout)
                .foregroundStyle(.secondary)
            HStack {
                if let s = naming.savedURL { Text("保存しました: \(s.lastPathComponent)").foregroundStyle(.green) }
                if !naming.saveError.isEmpty { Text(naming.saveError).foregroundStyle(.red) }
                Spacer()
                Button("フォルダを開く") { rec.openFolder(naming.folder) }
                Button("閉じる") {
                    naming.stopPlaying()
                    dismiss()
                }
                Button("名前を付けて保存") {
                    do {
                        let u = try naming.save()
                        naming.savedURL = u
                        naming.saveError = ""
                        rec.named(u)
                    } catch let e {
                        naming.saveError = "保存できませんでした: \(e.localizedDescription)"
                    }
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 780, height: 620)
        .onChange(of: naming.namesText) { _, v in
            rec.participants = v
            let n = Naming.split(v).count
            if n > 0 && n != naming.groupCount {
                naming.groupCount = n
                naming.regroup()
            }
        }
    }
}

struct GroupRow: View {
    @ObservedObject var naming: Naming
    let group: Naming.Group

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("グループ \(groupLetter(group.id))").bold()
                Text("\(group.lineIndices.count) 行・約 \(Int(group.seconds)) 秒").foregroundStyle(.secondary)
                Button {
                    if naming.playing == group.id { naming.stopPlaying() } else { naming.play(group.id) }
                } label: {
                    Label(naming.playing == group.id ? "止める" : "聞く",
                          systemImage: naming.playing == group.id ? "stop.fill" : "play.fill")
                }
                Spacer()
                Picker("だれ？", selection: Binding(
                    get: { naming.groups.first { $0.id == group.id }?.name ?? "" },
                    set: { v in
                        if let i = naming.groups.firstIndex(where: { $0.id == group.id }) { naming.groups[i].name = v }
                    })) {
                    Text("選ぶ").tag("")
                    ForEach(naming.names, id: \.self) { Text($0).tag($0) }
                    Text("わからない").tag(Naming.unknown)
                }
                .frame(width: 220)
            }
            ForEach(naming.exampleTexts(group), id: \.self) { t in
                Text("「\(t)」").font(.callout).foregroundStyle(.secondary).lineLimit(1)
            }
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.secondary.opacity(0.08)))
    }
}
