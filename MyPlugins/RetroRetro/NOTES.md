# Retro Retro — 作業メモ（Claude 用）

音を「古っぽくする」全部入り（NOISE / WOBBLE / DISTORT / DIGITAL / SPACE / LOSS / ECHO ＋ MASTER）。
本人の Mac にある市販の同じ種類のプラグイン（aufx `xRC2` / `xlnA`）を build/rclab で測って合わせ込んだ。
2026-11-12 本人「そのまんま同じものを作って、名前は rc version2」→ 実在製品の名前に近すぎるので本人が「Retro Retro」に決定。
ミックスエンジニア係の判定は「形を変えれば価値あり」→ 本人「表は絞り、中身は全部」:
処理は全モジュール測って合わせる、表（MAIN）はモジュールごとに大きいつまみ 1 本＋種類、細かいつまみは DETAIL ページ。
相手の録音データ（ノイズの素材・リバーブの IR・プリセット）は写していない。ノイズ・リバーブは計算で作り、測った数値（量・帯域の形・長さ・最初の遅れ）だけ合わせた。

## ID・コード
- PLUGIN_CODE `Rtrt`、Plugin Link の Kind 26（PluginLink.h には未登録。Shared を変えないため。25 = Twin Take）
- パラメーターは全部 0..1（本物と同じ数え方）か選択肢。一覧は PluginProcessor.cpp の `rrp::defs()`（ID は rr::Knobs の名前と同じ）。
  初期値は本物を立ち上げたときの値（NOISE 12 %・WOBBLE 41 %・DISTORT Tape 25 %・DIGITAL 6 %・SPACE 10 %・LOSS 15 %・ECHO 15 %、EQ ON 59 Hz〜5 kHz、COMPRESS 40 %、IN WIDTH 60 %）
- 版のヒント: 全部 1、gainMatch は 10。linkGroup、linkDuck（NOISE DUCK、0〜24 dB）
- echoClear は押したら 1 回だけ消して OFF に戻る（MessageManager で戻す）。プリセットの対象外

## 音の処理（Source、JUCE なし）
- RetroRetroDSP.h（共通の部品・NOISE・WOBBLE）、RetroModules.h（DISTORT・DIGITAL・SPACE）、RetroEngine.h（LOSS・ECHO・VARISPEED・MASTER・Engine）、
  RetroDistTables.h（自動生成: 歪み 9 種類の数値と SPACE の量合わせ。build/rclab/fit/gen_tables.py）
- 並び: IN GAIN → IN WIDTH → NOISE(前) → WOBBLE → DISTORT → DIGITAL → SPACE → LOSS → NOISE(後) → ECHO → VARISPEED → EQ → COMPRESS → OUT WIDTH → OUT GAIN → 頭打ち（0.7 から丸める）→ MIX
- MAGNITUDE は全モジュールの量に掛け算（本物で確かめた: MAG 50 % × 量 100 % ＝ MAG 100 % × 量 50 %）
- 遅れ: DISTORT の 4 倍（My Tremolo の Oversampler4、28）＋ CODEC の窓 N − 1（48 kHz 以下 N = 256、〜96 kHz 512、それ以上 1024）。つまみに関係なくいつも同じ。
  DISTORT と CODEC はいつも通す（量 0 でもそのまま通る）。MIX の元の音も同じだけ遅らせる
- 4 倍オーバーサンプラーは My Tremolo の TremoloStyle.h を include（**My Tremolo の Source を変えたら Retro Retro もビルド**）
- モジュールの FLUX: 量をゆっくりランダムに揺らす（＋入力の大きさに少し追従）。Filter Lo/Hi は Focus（その帯域だけに効かせる）/ Cut（効果で変わった分だけ帯域で切る）

### 測って合わせたこと（build/rclab、コミットしない）
- 測定ホスト `th`（AVAudioEngine オフライン）。**本物は最初の 1 ブロックを鳴らしてから RunLoop を回さないとリバーブの元データが読み込まれない**（PRIME=3 秒）。
  **入力が完全な無音だと全部止まる**（ノイズもリバーブも）→ 測るときは 25 Hz・−30 dB の小さい音を足して 80〜90 Hz の HPF で除く。
  状態（jucePluginState）は "COTT" ＋ 名前\0値\0 の並び。STATE=名前=値 で書き換えられる（リバーブの種類 Space_Type、ノイズの種類 Noise_NoiseType は AU のパラメーターにない）
- 素通し: 全部 0 でも本物は DISTORT の段で +0.7〜1.0 dB 付く（9 種類とも同じ）→ kBase 0.7 dB で固定（量 8 % までは頭打ちの形を通さず混ぜる = 量 0 は完全に線形）
- NOISE: 量 → 大きさ（10 % −67、25 % −48、50 % −32、100 % −20 dBFS）、TONE は 1 kHz 中心の傾き（(TONE−0.5) × 7.2 dB/oct）、CUT は 2 次、
  FOLLOW / DUCK は入力の大きさで dB を動かす形。入力が止まって 1.5 秒でノイズも止まる（本物と同じ）。
  種類は本物の 16 種類のうち 10 種類の帯域の形・左右の似かた・ゆっくりした揺れ・プチプチの数を測り、白色ノイズを 9 帯域の EQ で同じ形にして作る（素材は使っていない）
- WOBBLE: 遅らせる時間を揺らす。WOW 0.25〜4 Hz（0.25 × 16^つまみ）・最大 50 ms（0.5 Hz より遅いと浅く）、FLUTTER 6〜20 Hz・6 Hz で 7.2 ms。
  WOW<>FLUTTER は両方 0〜1 の重み（50 % で両方最大）。量の曲がり方は表（25 % で 0.117 倍…）。音程の揺れの標準偏差で本物と ±10 % 以内
- DISTORT: 9 種類それぞれ「前の EQ（低域・高域シェルフ・低域カット）→ 量で dB 足す → 入口の大きさで利得を下げる → 非対称・硬さ・折り返しのある頭打ち → 戻す」を
  build/rclab/fit/fitdist.py で当てはめ（1 kHz の音量 −48〜0 dBFS の曲線・倍音・小さい音の周波数特性）。誤差（重み付き dB の RMS）:
  Tube Pair 1.6 / Iron Core 0.9 / Crunch 1.1 / Heavy 2.1 / Zap 2.9 / Air Pressure 1.5 / Tape 1.3 / Classic Pre 1.5 / Pocket Recorder 4.6（本物は大きい音で急に −19 dB 落ちる AGC があり再現しきれていない）
- DIGITAL: 量 → サンプルレート（20 % 21 kHz、50 % 6.4 kHz、100 % 1.4 kHz。折り返しの位置で本物と一致）、ビットは μ-law で圧縮してから丸める（3 次倍音で本物と ±2 dB）。
  RATE<>BITS は両方 0〜1 の重み
- SPACE: 8 本の FDN ＋入口の拡散 4 段、ばねの 2 種類は全通過 6 段を足す。10 種類の量・最初の遅れ・EDT・T30 を本物と合わせた（量は ±0.1 dB、T30 ±10 %）。
  量 50 % でウェット最大、そこから上は元の音が減る（本物と同じ）。DECAY（本物の画面だけのつまみ、初期値 80 %）で長さ 0.25〜1.6 倍
- LOSS: MAGNETIC は音量の揺れ（平均 −0.9 dB・ばらつき 16 %）か高域の揺れ、音切れ。CODEC は STFT（√ハン、4 分の 1 ずつ）で小さい帯域を消す・大きさを段にする・位相を崩す・枠ごとに音量を上下。
  コヒーレンス（1 kHz）で本物と近い所に合わせた（量 75 % 0.67・100 % 0.62、本物 0.86・0.69）。本物の CODEC はもっと音量が揺れる（ばらつき −5 dB）が、そこまではしていない
- ECHO: 元の音と響きを同じパワーで入れ替え（cos / sin）、自由な時間は 12 ms〜2.2 s（表）、同期は 1/32〜2/1。フィードバックは 99 % まで輪の利得 0.97 × FB（必ず消える）、99〜100 % で 1.04 まで上げて tanh で頭打ちしながら鳴り続ける。
  輪の中に DISTORT（tanh）・DIGITAL（サンプルホールド）・SPACE（全通過 4 段）・LOSS（高域カット＋音量の揺れ）・WOBBLE、GRAINS（2 つの読み口で音程変更・逆再生・反転）
- MASTER: CUT は 1 次（Soft）か 4 次で山あり（Hard）、TONE の Tilt / Mid は測った形、Filter 1 / 2 はそれっぽい形（本物とは細部が違う）。
  COMPRESS は全部の大きさにかかる上下両方向（傾き 25 % 0.36・50 % 0.56・100 % 0.75、中心 −10.2 dB）。本物と ±0.2 dB。
  小さい音の持ち上げは −45 dB から戻して −65 dB で 0（DSP 検査係: ノイズ・残響の尾を +15 dB 持ち上げていたため。本物より控えめ）
- WIDTH: 0.5 でそのまま、下げると S を 0 へ、上げると S を最大 2 倍・M を 1/2

### つまみの切替（DSP 検査係の指摘で 1.0.0 の前に直した）
- ON/OFF: WOBBLE / DIGITAL / SPACE / ECHO は Engine の ModuleFade（15 ms で混ぜる。消え切ったら処理を止め、次の ON で中身を消す = 前の音が鳴らない）。DISTORT は MIX を 15 ms で
- DISTORT の種類: 2 つの Distort を 4 ms 待って 25 ms で混ぜる。SPACE の種類: 2 つの Space を 40 ms で混ぜる。SPACE の DECAY は遅れの長さを変えずに 32 サンプルごと
- EQ: CUT・TONE は 30 ms でなめらか＋32 サンプルごとに係数を作り直す。Hard・MODE・ON は古い EQ と 20 ms で混ぜる
- VARISPEED: 読み口 2 つで RESYNC を 10 ms 混ぜる。ON/OFF も 10 ms。WIDTH は 20 ms、WOBBLE の揺れ幅は 60 ms、ECHO SOLO は 10 ms
- ECHO CLEAR: Engine が押した瞬間だけ消す。ボタンを戻すのはエディターのタイマー（音の処理の中で callAsync しない）
- NOISE: 前用・後ろ用で別のノイズ（ブロックの大きさで音が変わらない）。10 種類の係数は prepare で先に作る（192 kHz で切替のたびに 2.5 ms かかっていた）
- 準備したより長いブロックは Engine が分けて処理する。GAIN MATCH の後にもう一度頭打ち（0 dBFS を超えない）

### テスト・当てはめの道具
- tools/rrlib.cpp … ctypes から呼ぶ C の入口（rr_render・rr_render2 / 3（途中で切替）・rr_dist・rr_set_dist・rr_set_space_trim）。プラグインには入らない
- build/rclab/fit（コミットしない）: rr.py、fitdist.py、spacetrim.py、chk1〜3.py（本物との比較）、sanity.py（NaN・ピーク・鳴り止むか・CPU）

## 版
- 1.0.0（2026-11-12）: 最初の版
