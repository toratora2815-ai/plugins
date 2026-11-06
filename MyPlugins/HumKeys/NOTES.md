# Hum Keys — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Hum Keys（HumKeys）… 鼻歌・口ドラムをその場で MIDI にする単体 Mac アプリ。仮想 MIDI「Hum Keys」（MidiOutput::createNewDevice）で Logic へ。
MELODY: 12 kHz に間引いて YIN で約 2.7 ms ごとに音程 → KEY / SCALE に合わせる（今の音を少しひいき）、GLUE / HOLD / OCTAVE / VELOCITY / BEND（±2 半音）/ MIDI CH、
音量のくぼみ→戻りで同じ音を打ち直す。DRUMS: 全体と 3 kHz 以上の包絡の立ち上がりで検出、14 ms の 150 Hz 以下・7 kHz 以上の割合（直前の残りを引く）で
KICK / SNARE / HAT（36 / 38 / 42）、LEARN（4 回で声の癖を覚える、settings の protoN_k）、SENS。DSP は VoiceEngine.h（JUCE なし、単体テスト可）。
マイク許可は MicPermission_mac.mm（AVCaptureDevice、許可待ちで画面を止めない）。`--snapshot <フォルダ>` で画面を PNG に書き出す（確認用）
v1.1.0: 本人の声（低い男声、F1〜A2）で調整: 音程を 5 回の中央値でならす、約 16 ms で 0.4 半音以上動く間は音を変えない、鳴っている間は YIN の判定を甘く（0.35）。
句の最初は 8 ms で鳴らし、120 ms 以内に高さが変わったら鳴らし直し（記録では kind=1 で最初の音の高さを直す）。遅れ約 56→32 ms。
TAKE（録り逃し防止）: Engine::capture に「本当に鳴り始めた時刻」付きで MIDI をため、4 秒黙ると ~/Music/Hum Keys に .mid 保存。
画面下からドラッグで Logic へ（performExternalDragDropOfFiles）、TEMPO（settings "tempo"）で拍に合わせる、フォルダ / NEW
v1.2.0: 口ドラム 6 音（KICK/SNARE/HAT/OPEN HAT/TOM/CLAP、36/38/42/46/45/39）。特徴は 14 ms の 6 帯域の割合（log10）、最寄りの覚えた音。
OPEN HAT / TOM / CLAP は LEARN してから（Params::has）。OPEN HAT を覚えるとハイハット系は 45 ms まで見て高音の残り方（protoSus）で開閉を決める。
settings は dproto{c}_{k} / dsus{c} / dhas{c}（前の "proto" は使わない）。AUTO（KeyFinder.h、Krumhansl で KEY・メジャー/マイナー、OCTAVE は真ん中が 66 付近）、
AUTO GATE（2 秒の雑音の 9 割点 +10 dB）。Logic 同期: 仮想 MIDI 入力「Hum Keys」で MIDI クロック＋SPP を受ける（SyncClock.h）、
音のサンプル番号→時刻（mapHostMs/mapClock、入力遅れを引く）→拍。同期テイクは小節頭からの MIDI（ファイル名 " barN"）、Logic 停止で区切る。
試し聴き（Preview.h、別の AudioDeviceManager で標準出力、簡単なシンセ・GM ドラム）、履歴（HistoryPanel、▶・ドラッグ・ゴミ箱）、
MINI（M）/ PIN（P、MacSupport_mac.mm の setFloatOnTop＝Num Deck と同じ Accessory＋全 Space）。分散通知 "com.myplugins.humkeys.command" で
toggle/on/off/melody/drums/mode/new/preview/history/autoKey/autoGate/octUp/octDown/mini/pin/panic/show を受ける
v1.3.0: AUTO（mode 2、音ごとに鼻歌か口ドラム。7 kHz〜の割合 feat[5] > -0.7 ならすぐドラム、ほかは 90 ms 聞いて autoVerdict＝声の割合 0.28 以上・高さの幅（2〜8 割点）3 半音未満・
6 dB 以内なら鼻歌。ドラムは ch10（autoDrumChannel）、CapEvent kind 2）。息よけ（breathGuard、3 kHz〜が -12 dB より多いフレームは声なし）。
EXPR（expression、CC1 = ビブラートの深さ、CC11 = 声の大きさ）。Metronome.h（CLICK・COUNT-IN、標準出力、耳に届く時刻で拍の目盛り、カウントの小節は負の拍）。
テイクの拍は takeSource（Logic / クリック / なし）。exportNotes が拍・小節頭・クオンタイズ（QUANTIZE / STRENGTH、出だしだけ寄せる）をまとめ、保存・ドラッグ・試し聴き・画面が同じものを使う。
TIME SIG（4/4〜12/8、settings "sig"）。MIDI はメロディーとドラムの 2 トラック（ドラムのトラックに "Hum Keys drums"）。
EDIT（EditPanel、選ぶ・上下・消す・⌘Z・最初の状態。takeLocked = 手直し済み、次の音から新テイク）。命令 auto/click/count/edit/quantize、キー 3/C/R/E/Q。
Num Deck 1.21.1 にその 5 つ。Idea Catcher 1.1.1 に VoiceEngine.h を写した
v1.4.0: 「Logic へ」（I キー、macsupport::insertMidiIntoLogic。AX で「読み込む → MIDI…」を押し ⇧⌘G でパス。Logic は再生ヘッド＝小節頭に置くので、
同期テイクは先に「/」（位置へ移動）で "N 1 1 1"。アクセシビリティ許可が要る）。LOOP（L キー、LENGTH 1/2/4/8 小節、takeLoop 拍。rebuildTakeNotes で周をたたむ、
1/8 拍早めはその周の頭、カウント中は捨てる。Metronome::setLoop で前の周を ToySynth（Preview.h）で鳴らす。⌫ / 命令 loopUndo で最後の周を消す）。
命令 toLogic / loop / loopUndo。Num Deck 1.22.1 にその 3 つ
