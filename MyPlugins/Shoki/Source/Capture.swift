// 書記（Shoki）— 音の入り口
// マイク（自分の声）と、Mac から出ている音（オンライン会議の相手の声）を受け取り、
// どちらも 16 kHz・モノ・Float32 にそろえて渡す。
import Foundation
import AVFoundation
import CoreAudio
import AudioToolbox
import Accelerate

let workRate: Double = 16000
let workFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 16000, channels: 1, interleaved: false)!

enum ShokiError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self { case .message(let m): return m }
    }
}

/// どんな形の音でも 16 kHz・モノ・Float32 にする（つなぎ目が切れないよう、変換器を持ち続ける）
/// 3 チャンネル以上（オーディオインターフェースなど）は、最初の 2 チャンネル（入力 1・2）だけを混ぜる
/// （全部を混ぜると声のない入力に埋もれて無音になる。後ろのループバックなどに Mac の音が戻ってくることもある）
final class Downsampler {
    private var converter: AVAudioConverter?
    private var inFormat: AVAudioFormat?
    private var monoFormat: AVAudioFormat?

    func process(_ buf0: AVAudioPCMBuffer) -> [Float] {
        guard buf0.frameLength > 0 else { return [] }
        var buf = buf0
        let nch = Int(buf0.format.channelCount)
        if nch >= 3, !buf0.format.isInterleaved, let data = buf0.floatChannelData {
            let n = Int(buf0.frameLength)
            if monoFormat == nil || monoFormat!.sampleRate != buf0.format.sampleRate {
                monoFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: buf0.format.sampleRate,
                                           channels: 1, interleaved: false)
            }
            guard let mf = monoFormat, let mb = AVAudioPCMBuffer(pcmFormat: mf, frameCapacity: buf0.frameLength) else { return [] }
            mb.frameLength = buf0.frameLength
            let dst = mb.floatChannelData![0]
            var half: Float = 0.5
            vDSP_vasm(data[0], 1, data[1], 1, &half, dst, 1, vDSP_Length(n))   // (入力1 + 入力2) × 0.5
            buf = mb
        }
        if converter == nil || inFormat == nil || !inFormat!.isEqual(buf.format) {
            inFormat = buf.format
            converter = AVAudioConverter(from: buf.format, to: workFormat)
            converter?.downmix = true
        }
        guard let conv = converter else { return [] }
        let cap = AVAudioFrameCount(Double(buf.frameLength) * workRate / buf.format.sampleRate) + 512
        guard let out = AVAudioPCMBuffer(pcmFormat: workFormat, frameCapacity: cap) else { return [] }
        var given = false
        var err: NSError?
        let src = buf
        let st = conv.convert(to: out, error: &err) { _, status in
            if given { status.pointee = .noDataNow; return nil }
            given = true
            status.pointee = .haveData
            return src
        }
        if st == .error || out.frameLength == 0 { return [] }
        return Array(UnsafeBufferPointer(start: out.floatChannelData![0], count: Int(out.frameLength)))
    }
}

struct AudioInputDevice: Hashable, Identifiable {
    let id: AudioDeviceID
    let uid: String
    let name: String
}

enum AudioDevices {
    static func inputDevices() -> [AudioInputDevice] {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr else { return [] }
        var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else { return [] }
        return ids.compactMap { id in
            guard inputChannels(id) > 0,
                  let uid = stringProp(id, kAudioDevicePropertyDeviceUID),
                  let name = stringProp(id, kAudioObjectPropertyName),
                  !uid.hasPrefix("Shoki") else { return nil }
            return AudioInputDevice(id: id, uid: uid, name: name)
        }
    }

    static func inputChannels(_ id: AudioDeviceID) -> Int {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration,
                                              mScope: kAudioDevicePropertyScopeInput,
                                              mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: 16)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
        let abl = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return abl.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    static func stringProp(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector) -> String? {
        var addr = AudioObjectPropertyAddress(mSelector: sel,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &value) == noErr, let v = value else { return nil }
        return v.takeRetainedValue() as String
    }

    static func nominalRate(_ id: AudioObjectID) -> Double? {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyNominalSampleRate,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var rate = Float64(0)
        var size = UInt32(MemoryLayout<Float64>.size)
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &rate) == noErr, rate > 0 else { return nil }
        return rate
    }

    static func defaultDevice(_ sel: AudioObjectPropertySelector) -> AudioDeviceID? {
        var addr = AudioObjectPropertyAddress(mSelector: sel,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var dev = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev) == noErr,
              dev != 0 else { return nil }
        return dev
    }
}

/// マイク（自分の声）。AVAudioEngine で受ける。機器が変わったら自動でつなぎ直す。
/// つなぎ直しに失敗しても、間をあけてやり直し続ける（会議の終わりまで止まったままにしない）
final class MicCapture {
    private var engine = AVAudioEngine()
    private var observer: NSObjectProtocol?
    private var wanted = false          // 「終える」まで true
    private var retryDelay = 0.3
    private var retryScheduled = false
    var onSamples: (([Float]) -> Void)?
    var onRestart: (() -> Void)?
    var deviceUID: String?              // nil = システムの標準のマイク

    func start() throws {
        wanted = true
        try startEngine()
    }

    private func startEngine() throws {
        engine = AVAudioEngine()
        let input = engine.inputNode
        if let uid = deviceUID,
           let dev = AudioDevices.inputDevices().first(where: { $0.uid == uid }),
           let au = input.audioUnit {
            var id = dev.id
            AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                 &id, UInt32(MemoryLayout<AudioDeviceID>.size))
        }
        let fmt = input.outputFormat(forBus: 0)
        guard fmt.sampleRate > 0, fmt.channelCount > 0 else { throw ShokiError.message("マイクが見つかりません") }
        let down = Downsampler()        // つなぎ直すたびに新しく（前の受け口と取り合わない）
        input.installTap(onBus: 0, bufferSize: 4096, format: nil) { [weak self] buf, _ in
            let s = down.process(buf)
            if !s.isEmpty { self?.onSamples?(s) }
        }
        engine.prepare()
        do {
            try engine.start()
        } catch {
            input.removeTap(onBus: 0)
            throw error
        }
        observer = NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange,
                                                          object: engine, queue: .main) { [weak self] _ in
            self?.restart()
        }
    }

    private func teardown() {
        if let o = observer { NotificationCenter.default.removeObserver(o); observer = nil }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
    }

    /// 機器の変更や、音が来なくなったときに呼ぶ（メインスレッドから）
    func restart() {
        guard wanted, !retryScheduled else { return }
        teardown()
        attempt()
    }

    private func attempt() {
        retryScheduled = false
        guard wanted else { return }
        do {
            try startEngine()
            retryDelay = 0.3
            onRestart?()
        } catch {
            let d = retryDelay
            retryDelay = min(4, retryDelay * 2)
            retryScheduled = true
            DispatchQueue.main.asyncAfter(deadline: .now() + d) { [weak self] in self?.attempt() }
        }
    }

    func stop() {
        wanted = false
        teardown()
    }
}

/// Mac から出ている音（自分以外の全部のアプリ）。プロセスタップで受ける。
/// 元の音は消さない（.unmuted）ので、会議の相手の声はふつうに聞こえる。
final class SystemCapture {
    private var tapID = AudioObjectID(kAudioObjectUnknown)
    private var aggID = AudioObjectID(kAudioObjectUnknown)
    private var procID: AudioDeviceIOProcID?
    private var curRate: Double = 0
    private let ioQueue = DispatchQueue(label: "shoki.systemtap", qos: .userInitiated)
    private var watchdog: Timer?
    private var lastGood = Date()
    private let lock = NSLock()
    var onSamples: (([Float]) -> Void)?
    var onRestart: (() -> Void)?
    private(set) var running = false

    func start() throws {
        // 最初につながらなくても、見張りは動かす（2 秒ごとにつなぎ直しを試し続ける）
        running = true
        let t = Timer(timeInterval: 2, repeats: true) { [weak self] _ in self?.checkAlive() }
        RunLoop.main.add(t, forMode: .common)
        watchdog = t
        try startTap()
    }

    func stop() {
        running = false
        watchdog?.invalidate()
        watchdog = nil
        stopTap()
    }

    private func startTap() throws {
        let desc = CATapDescription(stereoGlobalTapButExcludeProcesses: [])
        desc.uuid = UUID()
        desc.name = "Shoki"
        desc.isPrivate = true
        desc.muteBehavior = .unmuted
        var tap = AudioObjectID(kAudioObjectUnknown)
        var st = AudioHardwareCreateProcessTap(desc, &tap)
        guard st == noErr else { throw ShokiError.message("Mac の音を受け取れませんでした（\(st)）") }
        tapID = tap

        var asbd = AudioStreamBasicDescription()
        var size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        var addr = AudioObjectPropertyAddress(mSelector: kAudioTapPropertyFormat,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        st = AudioObjectGetPropertyData(tapID, &addr, 0, nil, &size, &asbd)
        guard st == noErr, asbd.mChannelsPerFrame > 0 else {
            stopTap()
            throw ShokiError.message("Mac の音の形式が読めませんでした（\(st)）")
        }

        var dict: [String: Any] = [
            kAudioAggregateDeviceNameKey: "Shoki Tap",
            kAudioAggregateDeviceUIDKey: "Shoki-" + UUID().uuidString,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceTapListKey: [
                [kAudioSubTapDriftCompensationKey: true, kAudioSubTapUIDKey: desc.uuid.uuidString]
            ]
        ]
        if let out = AudioDevices.defaultDevice(kAudioHardwarePropertyDefaultOutputDevice),
           let uid = AudioDevices.stringProp(out, kAudioDevicePropertyDeviceUID) {
            dict[kAudioAggregateDeviceMainSubDeviceKey] = uid
            dict[kAudioAggregateDeviceSubDeviceListKey] = [[kAudioSubDeviceUIDKey: uid]]
        }
        var agg = AudioObjectID(kAudioObjectUnknown)
        st = AudioHardwareCreateAggregateDevice(dict as CFDictionary, &agg)
        guard st == noErr else {
            stopTap()
            throw ShokiError.message("Mac の音の受け口を作れませんでした（\(st)）")
        }
        aggID = agg

        // 速さ（サンプルレート）は集約デバイスのものを使う（AirPods の通話モードなどで変わるため）。
        // チャンネル数と並び方はタップの形式から
        let rate = AudioDevices.nominalRate(aggID) ?? asbd.mSampleRate
        curRate = rate
        let interleaved = (asbd.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0 && asbd.mChannelsPerFrame > 1
        guard let fmt = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: rate,
                                      channels: AVAudioChannelCount(asbd.mChannelsPerFrame), interleaved: interleaved) else {
            stopTap()
            throw ShokiError.message("Mac の音の形式が作れませんでした")
        }

        var pid: AudioDeviceIOProcID?
        let down = Downsampler()        // つなぎ直すたびに新しく
        st = AudioDeviceCreateIOProcIDWithBlock(&pid, aggID, ioQueue) { [weak self] _, inData, _, _, _ in
            self?.handle(inData, fmt, down)
        }
        guard st == noErr, let p = pid else {
            stopTap()
            throw ShokiError.message("Mac の音を受け取れませんでした（\(st)）")
        }
        procID = p
        st = AudioDeviceStart(aggID, p)
        guard st == noErr else {
            stopTap()
            throw ShokiError.message("Mac の音を受け取れませんでした（\(st)）")
        }
        lock.lock(); lastGood = Date(); lock.unlock()
    }

    /// タップの音は、入力バッファの「最後」に並ぶ（出力機器にマイクがあると、その分が先に来る）
    private func handle(_ inData: UnsafePointer<AudioBufferList>, _ fmt: AVAudioFormat, _ down: Downsampler) {
        let all = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: inData))
        let need = fmt.isInterleaved ? 1 : Int(fmt.channelCount)
        guard need > 0, all.count >= need else { return }
        let first = all.count - need
        let bpf = Int(fmt.streamDescription.pointee.mBytesPerFrame)
        guard bpf > 0 else { return }
        let frames = Int(all[first].mDataByteSize) / bpf
        guard frames > 0, let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(frames)) else { return }
        buf.frameLength = AVAudioFrameCount(frames)
        let dst = UnsafeMutableAudioBufferListPointer(buf.mutableAudioBufferList)
        guard dst.count == need else { return }
        for i in 0..<need {
            guard let s = all[first + i].mData, let d = dst[i].mData else { return }
            memcpy(d, s, min(Int(all[first + i].mDataByteSize), Int(dst[i].mDataByteSize)))
        }
        lock.lock(); lastGood = Date(); lock.unlock()
        let x = down.process(buf)
        if !x.isEmpty { onSamples?(x) }
    }

    private func stopTap() {
        if aggID != AudioObjectID(kAudioObjectUnknown) {
            if let p = procID {
                AudioDeviceStop(aggID, p)
                AudioDeviceDestroyIOProcID(aggID, p)
            }
            AudioHardwareDestroyAggregateDevice(aggID)
        }
        if tapID != AudioObjectID(kAudioObjectUnknown) { AudioHardwareDestroyProcessTap(tapID) }
        procID = nil
        aggID = AudioObjectID(kAudioObjectUnknown)
        tapID = AudioObjectID(kAudioObjectUnknown)
    }

    /// イヤホンの抜き差しなどで音が来なくなった・速さが変わったら、つなぎ直す
    private func checkAlive() {
        guard running else { return }
        lock.lock(); let age = Date().timeIntervalSince(lastGood); lock.unlock()
        var rateChanged = false
        if aggID != AudioObjectID(kAudioObjectUnknown), let r = AudioDevices.nominalRate(aggID), abs(r - curRate) > 1 {
            rateChanged = true
        }
        if age > 3 || rateChanged || aggID == AudioObjectID(kAudioObjectUnknown) {
            stopTap()
            if (try? startTap()) != nil { onRestart?() }
        }
    }
}
