// テスト用：AU エフェクトにオフラインで音を通して WAV に書き出す
//   auhost list
//   auhost render in.wav out.wav 余韻秒 名前=値 ...   (値は 0..1 の正規化値、"#" を付けると実値)
// 相手は環境変数 AU_SUB / AU_MANU（4 文字）で選ぶ。WARM=ブロック数 で設定後に空回しする長さ
import AVFoundation
import AudioToolbox

func fourcc(_ s: String) -> UInt32 { s.utf8.reduce(0) { ($0 << 8) | UInt32($1) } }
let envv = ProcessInfo.processInfo.environment
let desc = AudioComponentDescription(componentType: kAudioUnitType_Effect,
                                     componentSubType: fourcc(envv["AU_SUB"] ?? "vee3"),
                                     componentManufacturer: fourcc(envv["AU_MANU"] ?? "oDin"),
                                     componentFlags: 0, componentFlagsMask: 0)
func load() -> AVAudioUnit {
    var res: AVAudioUnit?
    let sem = DispatchSemaphore(value: 0)
    AVAudioUnit.instantiate(with: desc, options: [.loadOutOfProcess]) { u, e in
        if let e = e { print("instantiate error", e) }
        res = u; sem.signal()
    }
    sem.wait()
    return res!
}
let args = CommandLine.arguments
let unit = load()
let au = unit.auAudioUnit
let params = au.parameterTree?.allParameters ?? []
if args[1] == "list" {
    for p in params {
        print(p.address, p.identifier, "|", p.displayName, p.minValue, p.maxValue, p.value, p.valueStrings ?? [])
    }
    exit(0)
}
let inURL = URL(fileURLWithPath: args[2]), outURL = URL(fileURLWithPath: args[3])
let tail = Double(args[4])!
let inFile = try! AVAudioFile(forReading: inURL)
let fmt = AVAudioFormat(standardFormatWithSampleRate: inFile.fileFormat.sampleRate, channels: 2)!
let inBuf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(inFile.length))!
try! inFile.read(into: inBuf)

let engine = AVAudioEngine()
let player = AVAudioPlayerNode()
engine.attach(player); engine.attach(unit)
engine.connect(player, to: unit, format: fmt)
engine.connect(unit, to: engine.mainMixerNode, format: fmt)
try! engine.enableManualRenderingMode(.offline, format: fmt, maximumFrameCount: 512)
try! engine.start()
for a in args.dropFirst(5) {
    let kv = a.split(separator: "=", maxSplits: 1).map(String.init)
    guard let p = params.first(where: { $0.identifier == kv[0] || $0.displayName == kv[0] }) else { print("no param", kv[0]); exit(1) }
    if kv[1].hasPrefix("#") { p.value = Float(kv[1].dropFirst())! }
    else { p.value = p.minValue + Float(kv[1])! * (p.maxValue - p.minValue) }
}
// 数値が反映されるまで少し無音を回す
let warm = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 512)!
let warmN = Int(envv["WARM"] ?? "200")!
for _ in 0..<warmN { _ = try! engine.renderOffline(512, to: warm) }
player.scheduleBuffer(inBuf, at: nil)
player.play()
let total = AVAudioFramePosition(inFile.length) + AVAudioFramePosition(tail * fmt.sampleRate)
var outSettings = fmt.settings; outSettings[AVFormatIDKey] = kAudioFormatLinearPCM
outSettings[AVLinearPCMBitDepthKey] = 32; outSettings[AVLinearPCMIsFloatKey] = true
let outFile = try! AVAudioFile(forWriting: outURL, settings: outSettings, commonFormat: .pcmFormatFloat32, interleaved: false)
let buf = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 512)!
var done: AVAudioFramePosition = 0
while done < total {
    let n = AVAudioFrameCount(min(512, total - done))
    let st = try! engine.renderOffline(n, to: buf)
    if st != .success { print("render status", st.rawValue); break }
    try! outFile.write(from: buf)
    done += AVAudioFramePosition(n)
}
outFile.close()
