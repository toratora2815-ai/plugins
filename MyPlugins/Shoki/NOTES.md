# 書記（Shoki）— 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・版ごとの変更の記録です。

## 仕組み
- Swift だけ（SwiftUI + AVFoundation + CoreAudio + Speech + Accelerate）。Xcode なしで `build.sh`（swiftc）。CMakeLists がないので GitHub Actions の detect には拾われない（ビルドしない）
  - Command Line Tools には SwiftUI のマクロのプラグインがないので **`@State` は使えない**（`@Published` / `@StateObject` / `@ObservedObject` / `@EnvironmentObject` は使える）。`@Observable` などのマクロも不可
- 署名は build.sh が「MyPlugins Local Signing」で（マイク・システムオーディオ録音の許可が作り直しで外れないように）。bundle id `com.myplugins.shoki`
- 音の入り口（Capture.swift）
  - マイク: AVAudioEngine の inputNode タップ。機器を選ぶときは AUHAL の kAudioOutputUnitProperty_CurrentDevice。設定が変わったら（AVAudioEngineConfigurationChange）つなぎ直す
  - 相手: Core Audio のプロセスタップ `CATapDescription(stereoGlobalTapButExcludeProcesses: [])`（自分以外の全部の音。Meet は Chrome の補助プロセスから鳴るので、アプリを選ばない）、`muteBehavior = .unmuted`（Reference Monitor は消音 2 なので流用時に注意）、プライベートな集約デバイス（主は今の標準の出力）。2 秒ごとの見張りで、3 秒コールバックがなければつなぎ直す
  - どちらも AVAudioConverter（downmix）で 16 kHz・モノ・Float32 に
- 文字起こし（Transcriber.swift）: 流れごとに SpeechAnalyzer + SpeechTranscriber（ja_JP、volatileResults、audioTimeRange）。bestAvailableAudioFormat は 16 kHz・Int16・モノ
  - feed は直列キュー。時計（会議の開始時刻）より 0.5 秒以上遅れたら無音で埋める（つなぎ直しの抜けで時刻・音声ファイルがずれないように）。テストは fillGapsByClock = false
  - 確定した結果を「。？！」と 0.9 秒以上の間で文に分ける（1 つの結果に 2 人分入ることが多いので必須）。文ごとに SampleRing（900 秒）から音を取り、声の特徴を計算
  - 音声は 16 bit の caf に書き、止めたら m4a（AAC 32 kbps）に変換して caf を消す（長さが 98% 以上あるのを確かめてから）
  - AnalysisContext.contextualStrings に参加者の名前と言葉のヒント（日本語で効くかは未確認）
- 声の特徴（Voice.swift）: MFCC c1〜c19 の平均とばらつき（25 ms / 10 ms、40 メル、80〜7600 Hz、いちばん大きい所から 30 dB 以内のフレーム）＋声の高さ（正規化自己相関 70〜400 Hz、最初の山）の中央値と四分位幅 = 40 次元。外部のモデルは使わない
- 名前付け（Meeting.swift の Naming）: 相手の行だけを次元ごとに標準化（声の高さは重み 2、ばらつきは 0.7）→ 声が 1 秒以上の行で Ward 法 → k-means 8 回 → 全部の行を近いグループへ。2 番目との差 (d2−d1)/(d2+d1) が 0.06 以下か声が 0.6 秒未満なら「自信なし」（名前?）。特徴のない短い行は前後 3 秒以内が同じグループならそれ
  - グループの数の初期値 = 参加者の名前の数。グループは最初に話した順に A, B, C
  - 名前つき議事録は時刻順、同じ名前が 20 秒以内に続けば 1 段落。言いよどみ（えー・あのー など）は TextClean で消す
- 保存: `~/議事録/<yyyy-MM-dd HHmm> 会議/`（書類フォルダは iCloud 同期なので避けた）。記録（時刻つき）.md と data.jsonl は 1 行ごとに書き足して synchronize。JSON は NaN を文字列で
- 声紋は保存しない（同僚の声の特徴を次の会議に持ち越すのは本人たちの了承が要るので、1.0.0 ではやらない）
- アプリに Claude は入れない。清書は本人が Claude に「清書して」と頼む（手本＝前回の議事録の書き方）

## DSP 検査係の指摘で直したこと（1.0.0 を渡す前、2026-11-18）
- タップの音は集約デバイスの入力バッファの **最後** に並ぶ（出力機器にマイクがある AirPods・USB ヘッドセット・オーディオインターフェース・Teams Audio などは、その分が先に来る）。
  最後の need 本を AVAudioPCMBuffer にコピーして使う（bufferListNoCopy に全部渡すと nil で、相手がずっと無音だった）。レートは集約デバイスの NominalSampleRate（AirPods の通話モードで変わる）、
  見張りが 2 秒ごとにレートも見て、変わったら・3 秒まともな音が来なければつなぎ直す
- マイクのつなぎ直しは失敗してもやり直し続ける（0.3 → 最大 4 秒）。installTap は format: nil。Recorder.tick がマイクの音が 3 秒来なければ restart、相手が 5 秒来なければ画面に出す。メーターは 1 秒で 0
- 音声認識の裏のプロセスが落ちると結果の流れがエラーで終わる → 解析器を作り直し、最後に確定した所から輪っか（900 秒）で入れ直す（時刻は offset）。その間も音声ファイルには書き続ける。テストは shoki-test の第 3 引数 crash
- 3ch 以上のマイク（オーディオインターフェース）は downmix すると無音になる → 0.5 秒ごとにいちばん大きいチャンネルを使う
- 1 秒以上遅れていて、いまの塊が静かなら無音を詰める（見張りが気づかない短い途切れの時刻のずれ）
- 終了するときは仕上げ（最後の確定・圧縮）を待つ。圧縮は「-tmp.m4a」に書いてから名前を変える
- **スピーカーの回り込み**（本人の通しテストで発見。MacBook のスピーカー＋本体マイクだと、相手の発言がほぼ全部「自分」でも二重に書かれた）:
  自分の行は 4 秒待ってから確定し、前後 4 秒の相手の行の文字（カタカナ→ひらがな、記号を除く）に、自分の行の 2 文字の並びが 6 割以上（10 文字以下は 5 割以上）入っていれば捨てる（EchoFilter）。
  名前付き議事録を作るときも同じく捨てる（前の版で録った記録にも効く）。本人の 32 分のテストで 267 行中 236 行を捨て、残りは「そうですね」「はい」など本人の発言
- 本人の 32 分のテスト（実際の会議の音）: 相手 256 行、グループ 3 つで 99 / 85 / 68 行、自信なし 約 27%（読み上げ音声より難しい）

## テスト
- `bash build.sh test` → `build/Shoki/shoki-test <作業フォルダ> [速さ]`。読み上げ音声（Kyoko / Eddy / Grandpa を相手、Flo を自分）にフィルターで回線の違いを付けた 2 分の会議
  - 2 倍速: 名前 27/27 正解。50 倍速で 2 本同時に流すと文字が抜ける（解析が追いつかない。実時間では起きない）
  - 実時間 10 分: メモリ 30〜40 MB で一定、CPU 約 2%、時刻のずれ 0.2 秒以内、名前 107/0（自信なし 2）
  - DSP 検査係の 1 時間（3 倍速）: メモリ横ばい、拾えた割合 89〜96%、名前 587/1/9、圧縮 115 → 15 MB（2 秒）
  - 作業フォルダは scratchpad など（録音をリポジトリや build/ に置かない）
- `tools/render_ui.swift` … 画面をマウスなしで PNG に書き出す（`-D UITEST` で App の @main を外す）

## 版
- 1.0.0（2026-11-18）: 最初の版。本人「会議の書記を完全に代わりにしてほしい」→ ミックスエンジニア係「形を変えれば価値あり（会議中は文字起こしだけ、名前付けは会議のあとに 1 人 1 回、まとめの AI は入れない）」→ 本人がその形・オンライン・音声を残す（簡単に消せる）を選ぶ
