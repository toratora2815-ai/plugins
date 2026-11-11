# My Filter — 作業メモ（Claude 用）

音に合わせて動くフィルターの全部入り。本人の Mac にある市販のフィルター（aufx `FF1 ` / `FF2 `・`SToy`）を測って合わせ込んだ。
2026-11-11 本人「サウンドトイズでコピーすべきものは？」→ Filter Freak 系を選ぶ。ミックスエンジニア係は「形を変えれば価値あり
（フィルター 1 つ・FF2 とフェイザーは入れない・小さく）」→ 本人「そのまま同じものを」「名前は my filter」。
だから FF1 と FF2 を 1 つにまとめ（1 Filter / 2 Filters）、モード 6 つ・TWEAK の中身まで全部入れた。フェイザーは入れていない（本物の FilterFreak にもない）。

## ID・コード
- PLUGIN_CODE `Myfl`、Plugin Link の Kind 22（PluginLink.h には未登録。Shared を変えないため。21 = My Tremolo）
- 揺れの時計・形の表・パターン・自分の形・色付け（StyleChannel・4 倍オーバーサンプラー）は My Tremolo の Source をそのまま include している
  （CMakeLists で ../MyTremolo/Source）。**My Tremolo の Source を変えたら My Filter もビルド**（build.yml は自動では気づかない）
- パラメーター（版のヒント 1）: input, output（±24 dB）, mix 0..100, analog（Digital / Analog）, style（Clean, Fat, Squash, Dirt, Crunch, Shred, Pump）,
  filters（1 / 2）, routing（Serial / Parallel、既定 Parallel = 本物の FF2 と同じ）, filterLink,
  freq1/2（Hz、20〜20000 log、既定 4968 / 880）, res1/2 0..100, shape1/2（LP / BP / HP / Notch、既定 LP / BP）, poles1/2（2/4/6/8）, gain1/2 ±24 dB,
  freqMod1/2 ±10 oct（既定 +8）, resMod1/2 ±100, levelMod1/2 ±100, mod 0..100, modMode（LFO / RHYTHM / ENVELOPE / RANDOM / STEP / ADSR、既定 ENVELOPE）,
  lfoRate 0.01..100 Hz（既定 0.25）, lfoShape（15 種＋Custom）, rhythm（4 Bars〜1/32＋Pattern）, tempo 30..240, sync, groove ±25,
  threshold −60..0（既定 −20）, envGain 0..24 dB（既定 12 = ×4）, attack / release（既定 570 / 550 ms）, trigLevel, trigger（押している間 1）,
  adsrA/D/S/R, lrOffset ±2 oct, lrMode（Normal / Flipped）, smoothing, smoothMode, gainMatch（版 10）, linkGroup, linkDuck
- 自分のプリセット・内蔵プリセットから外しているもの: linkDuck, trigger（と gainMatch・linkGroup）
- パターン・自分の形は apvts.state の "pattern"・"customShape"（My Tremolo と同じ形式。.myrhythm / .myshape も同じ）

## 測ったこと（build/fflab、コミットしない。th = 測定ホスト、SUB 環境変数で "FF1 " / "FF2 "）
- 本物の遅れ 45 サンプル（自作は 29。4 倍の上げ下げの式では 28 だが、DSP 検査係の実測で山は 29・重心 28.6〜29.1 → 29 と申告、元の音の輪も 29。
  増えた 1 はオーバーサンプラーではなく、全開の LP 自体の遅れ（約 0.6）。同じ Oversampler4 の My Tremolo は山 28・重心 28.00・正弦波の位相 28.0 で、申告 28 のままで正しい（2026-11-11 実測））。つまみはホストに出ている: Bypass, InputGain, OutputGain（どちらも 0..1 = ±24 dB）, Mix, InOutMode（Digital/Analog）,
  Frequency（20 Hz × 1000^v、ぴったり）, Resonance, FilterOrder（0..1 を 3 倍して丸め = 2/4/6/8 極）, FilterShape, ModulationDepth, Trigger, Tempo（30..240）, LFO Rate。
  FF2 は Frequency1/2・Resonance1/2・Gain1/2（±24 dB）・Shape・Order、FilterLink、FilterRouting（Series/Parallel）
- 動かし方・FREQ MOD などはホストに出ていない（.toy は中身が暗号化。中は解かない）。初期状態とプリセット経由で測った
- **つまみを動かした直後は本物も 20 ms ほどかけて動く**。インパルスは 2 秒目に置いて測る（最初、21 ms 目に置いて周波数を取り違えた）
- **インパルス応答の FFT 窓は、45 サンプルの手前から取る**（直線位相の FIR の前ぶれを切ると HP の低域が −22 dB で平らに見える）
- フィルターの決まり（周波数 1 kHz、RES 0〜0.95 × 形 4 × 極 4 で確認、build/fflab/g・g2・g3）:
  - RES → 山の高さ P（dB）は表（resPeakDb、0.02 刻み）。K = P²
  - LP / HP: 2 次の段 n 個、段ごとの Q は「段の山の高さ = K^(1/n)」、全体の倍率 1/√K。BP: 正規化した帯域通過 n 個（Q = K^(1/n)）× √K。Notch: 1 − 正規化 BP、倍率 1
  - 4〜8 極だけ、RES 0.82 から上で全体の倍率が 2 極より下がり −24 dB 前後で止まる（multiPoleExtraDb の表）
  - 高い周波数ほど山が鋭くなる: 減衰 R から dampShift（5 kHz で約 0.009、20 kHz で約 0.022）を、R が小さいほど控えめに引く（effDamp）
  - 結果: RES 0.6 まで 30 Hz〜12 kHz の差の中央値 0.05〜0.2 dB、RES 0.8 で 0.4〜1.6 dB。山の高さは 632 Hz〜20 kHz・RES 0〜0.85 で ±1〜2 dB
  - RES 0.96 で約 39 dB（ゆっくり消える）、0.98・1.0 は無音からでも鳴り出し、約 0.9 の正弦波（3 倍音 −55 dB）。本物の無音時の雑音は約 −120 dB
    → 自作は 0.965 から上で減衰を負にし、帯域の状態を ±lim で止める（それ以外は v/(1+(v/lim)^4)^(1/4) のやわらかい天井、出口で +12 dB の余裕）。
    −140 dB の揺らぎを足して無音から鳴り出すようにした（1 kHz で 0.7 秒・r 0.98 で 1.5 秒 = 本物 0.55 / 1.1 秒）
- MIX: dry = cos(π/2·mix)、wet = sin(π/2·mix)（0.25・0.5・0.75 で +2.35・+3.04・+2.35 dB を確認）
- DIGITAL: 出口（元の速さ）で ±0.988 に硬く止める（4 倍の所だけで止めると戻す時のはね返りで 1.5 まで出た）
- ANALOG の色付けは Tremolator と同じ 7 種類（説明書の名前も同じ）。初期状態（Clean）を My Tremolo の Smooth と比べ、奇数倍音が ±0.5 dB で一致。
  フィルターの前（front）と後ろ（back）に分かれているのも、LP 200 Hz・HP 2.5 kHz に 1 kHz・100 Hz を通して確かめた。
  偶数倍音（2 倍音）は本物のほうが 6〜18 dB 多い（−39〜−45 dB）。フィルター自体が少し非対称に歪んでいるらしい。直していない
- ENVELOPE（初期状態、18 kHz の正弦波を音量 8 段で、周波数の動きを 10 ms ごとに読む）: しきい値 −20 dB、×4、
  「入力の大きさ → しきい値を超えた分 × GAIN → 0..1 で止める → その量を ATTACK（時定数約 125 ms）/ RELEASE（約 120 ms）でならす」。
  止めてからならすので、大きな音でも開く速さは同じ（本物どおり）。音量ごとの開く量・速さが 0.05 オクターブ以内で一致
- MOD と FREQ MOD: 周波数 = FREQ × 2^(MOD × FREQ MOD × 元)、元 0..1（上向きだけ）。MOD 0.25 / 0.5 で 2.0 / 4.0 オクターブ → 初期の FREQ MOD は約 8 オクターブ。
  動いても 19.6 kHz で止まる（MOD 1 で +6.02 oct から上は平ら）
- LFO の初期の速さ（0.317）= 約 0.25 Hz（三角波のプリセットで 4 秒周期）。RATE の目盛りの対応（つまみ位置 → Hz）は合わせていない（自作は 0.01〜100 Hz の対数）

## 測っていない・自分で決めたこと
- RHYTHM の中身は My Tremolo（Tremolator を測ったもの）の時計・形・GROOVE をそのまま使った（本物も同じ会社の共通部品で、リズムのファイルも共用されている）。
  ただし FEEL・ACCENT・RATE MOD・DEPTH MOD は FilterFreak の説明書にないので入れていない
- RANDOM（1 回ごとにでたらめな値、SMOOTH でなめらかに）、STEP（しきい値を上に越えるたびに新しい値、3 dB 下がったら次を受け付ける、SMOOTH × 200 ms で移る）、
  ADSR（A は直線、D・R は指数で 99 % に届く時間）の細かい形は説明書から決めた
- RES MOD（RES に MOD × 量 × 元を足す）・LEVEL MOD（+ = 元が小さい所で下げる、− = 大きい所で下げる）の範囲は説明書に数値がないので自分で決めた
- L/R OFFSET・L/R MODE・「2 つのとき 1 を左・2 を右」は説明書どおりモノ → ステレオのときだけ（ステレオ → ステレオでは効かない）
- LINK（1 と 2 を一緒に動かす）は画面で動かした時だけ（processor の listener、メッセージスレッドの時だけ）。片方が端に着くと、もう片方もそれ以上動かない

## DSP 検査係（2026-11-11）で直したこと
- 形・POLES・フィルターの数・Serial / Parallel の切り替えは、通った音だけを cos の形で 6 ms 下げ → 切り替え（形が変わった・2 つ目を使い始めたフィルターは空に）→ 6 ms で戻す（swPos / swGain）。
  段差の目安 −13 dB → −58〜−94 dB。POLES だけの変更では段を空にしない（残る段はそのまま、増えた段は setRes が空にする）
- RES・RES MOD・LEVEL MOD・MOD（× VOCAL DUCK）・FREQ MOD・L/R OFFSET も 20 ms でならす（オートメーションの段々 −43〜−62 dB → −95〜−121 dB）
- LINK は画面でつまみをつかんでいる間だけ（エディターの onDragStart / onDragEnd → linkGesture）。相手にも begin / endChangeGesture。
  前は自分のプリセット・取り消しの読み込みで相手が二重に動いた（1 オクターブずれ）
- 遅れ 28 → 29、テール 0.5 → 5 秒（RES 0.95 で −60 dB まで 1.8 秒）、±4 の安全止めは通った音だけ（元の音は削らない）、
  −140 dB の揺らぎは発振する設定（RES 0.965 から上）の時だけ、色付けを替えて重ねている間は DIGITAL の止めも重ねる
- 直していないこと: 並列 2 つ・Fat / Squash / Dirt / Pump・RES 高めで 0 dBFS 入力から +5〜+9 dBFS（本物も足すだけなので README に注意）、
  DIGITAL の 1 倍の速さでの硬い止めの折り返し（−34 dB、本物どおり）
- 検査の道具: build/fflab/inspect（t1 遅れ、t2 暴走、t3 切り替え、t4 段々、t5 CPU、t7 響きの長さ、t9 LINK の模型）

## 道具
- tools/render_myfilter.cpp … エンジンでファイルを処理（キーは先頭のコメント）。コンパイル: `clang++ -O2 -std=c++17 -I../../MyPlugins/MyTremolo/Source …`
- build/fflab/resp.py（インパルス応答 → 周波数特性）、track.py（10 ms ごとのインパルス列で、今の周波数を読む）、harm.py（倍音）

## 版
- 1.0.0（2026-11-11）: 最初の版
- 1.1.0（2026-11-11）: 曲全体用の内蔵プリセット 9 個（"Mix …"、kFactory の最後に足したので前の番号は同じ）。0 dBFS 近い曲（tanh で潰した RMS −10 dB も）で
  ピークは入力以下・RMS −1〜−6 dB（Radio は output +4 で −8 → 約 −4）。4 Bars の Ramp Up は小節 1 から 4 小節かけて開くのを確認
