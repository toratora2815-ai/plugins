# My Tremolo — 作業メモ（Claude 用）

トレモロの全部入り。本人の Mac にある市販トレモロ（aufx `TRM ` / `SToy`）を測って合わせ込んだ。
2026-11-10 本人「いろいろ実験して音の響きとか抑揚とか、そのまんま同じものを作って。名前は my tremolo」。
ミックスエンジニア係は「作る価値あり。ただしつまみ 10 個前後に絞り LOW KEEP を足すほうが実戦向き」→ 本人「そのまま全部」（LOW KEEP は足す約束）。

## ID・コード
- PLUGIN_CODE `Mytr`、Plugin Link の Kind 21（PluginLink.h には未登録。Shared を変えないため。17 AIO Guitar / 18 Vox Chop / 19 Drum Transient / 20 Deca Satu）
- パラメーター（版のヒント 1）: input, output（±24 dB）, depth 0..100, groove ±25 %, feel ±50 %, accent ±20 dB, width 0..100, shape（Choice 15 種＋Custom）,
  rhythm（Choice 4 Bars〜1/32＋Pattern）, rate 30..240 BPM, sync, style（Digital / Smooth / Even / Squeeze / Dense / Crunch / Grit / Pump）,
  threshold −80..0, attack / release 0.1..5000 ms, envMode（Env / Gate）, rateMod ±6 oct, depthMod ±100, lowKeep 0..400 Hz（20 未満 = OFF）,
  smoothing, smoothMode / gainMatch（版 10）, linkGroup, linkDuck（% 浅く。プリセット・取り消しの対象外）
- パターン（升目）と自分の形（点）はパラメーターではなく apvts.state の "pattern"・"customShape"（文字列）。自分のプリセット（PresetBar）には入らない →
  タブの SAVE で .myrhythm / .myshape（~/Library/Audio/Presets/MyPlugins/My Tremolo/Rhythms・Shapes）。元のトレモロも形・リズムのプリセットが別

## 音の処理（Source/MyTremoloDSP.h、TremoloStyle.h、JUCE なし）
- 流れ: INPUT（20 ms でならす）→ 4 倍（ハーフバンド FIR 47 タップ＋21 タップ、遅れ 28 サンプルちょうど。down は x0 → 畳み込み → x1 の順）→ 色付けの前半（強調 → コンプ → 1 段目の tanh）→ 揺れ（VCA、LOW KEEP なら LR4 で高い帯域だけ）
  → OUTPUT → 色付けの後半（2 段目の tanh → 強調を戻す → 低域の棚 → 2 次 HPF 約 11 Hz → 補正）→ 1 倍。Digital は何もせず最後に ±0.988 で硬くクリップ
- 時計: SYNC 中で再生中は Logic の ppq にぴったり。RATE MOD で速さが変わっている間は自走し、終わったら「いちばん近い小節の頭」（lockWrap: 4 拍、付点 12 拍、パターンはその長さ。ACCENT と GROOVE の区切りもそろう長さ）へ
  時定数 2.5 秒で寄せる（本物と同じ。すぐには戻らない）。再生位置が飛んだらすぐ合わせる（10 ms のつなぎ）。止まっている間・SYNC OFF は自走
- 1 回の揺れ（Modulator::gain）: GROOVE で 2 回の組を (1+g)・(1−g) の長さに → FEEL で各回を「その回の長さ × feel」だけずらす（+ = 遅れ）
  → 回の中の位置で形の表（256 点）→ ACCENT（2 拍ごとの頭の回。+ はほかを −A dB、− は頭を −A dB）。ずらしてできたすき間は形 0、重なりは大きいほう
  → 利得 = 1 − 深さ × (1 − 形)。休み（パターンの空き）も形 0
  - ただし、すき間・重なり・休みの入口と出口（ぴったり続く回がない所だけ）は 1.5 ms の傾きでつなぐ。本物は一気に 0 にする（ノコギリ波で確認）が、
    終わりの値が 0 でない形（Vintage・Round Square・Ramp）ではプツッと鳴るため（DSP 検査係 3 回目「直してから」、段差 0.99 → 0.014）。
    ACCENT の倍率が違う回の境目も同じ（4 回目で見つかった。Round Square・ACCENT ±20 で段差 0.89 → 0.014）。
    比べるのは組の頭からの手元の座標（拍の位置が大きくなっても境目の判定がずれない。96 万拍で確認）。
    GROOVE・FEEL・ACCENT が 0 のときは傾きがかからないので、本物との一致（fullcmp.sh 23 通り）は変わらない
- 自走が長く続いたら（1e6 拍）、リズム・ACCENT・GROOVE の区切りがそろう長さ（lockWrap）の倍数で拍を折り返す
- つなぎ（declick）: 飛んだ瞬間の「前の利得との差」を覚えて 10 ms でまっすぐ 0 に（その間も利得は 0〜1 に収める）。再生位置の飛び・ループ・SHAPE / RHYTHM の変更・パターンの取り込み、
  ブロックの途中のテンポ変更で 1 回の長さの 0.2 % 以上ずれたとき
- WIDTH: 右は左の揺れ全体を「width/2 × 1 回の長さ」だけ先に（一定の時間）
- 深さ: DEPTH MOD + なら DEPTH ×（1 − dm（1 − e））、− なら DEPTH ×（1 + dm·e）。e = 入力の量
- 入力の量 e: INPUT のつまみの前の、両チャンネルの |x| の大きいほう → ピーク（0.6 ms で戻る）→ ATTACK / RELEASE（表示の時間 = 99 % に届く時間、時定数はその 1/4.6）
  → Env: (env − しきい値) / 0.2 を 0..1 に。Gate: しきい値を超えたら 1（ATTACK / RELEASE でならす）
- 速さ: × 2^(RATE MOD × e)

## 測ったこと（build/tremlab、コミットしない）
- 測定ホスト `th`（AVAudioEngine オフライン、v2 API）。**HostCallbacks（拍・テンポ・再生中）は engine.start() の後に入れないと効かない**
- 内蔵プリセット 142 個（/Users/Shared/Soundtoys/Soundtoys 5/Tremolator/*.toy、中身は暗号化）は ClassInfo の "soundtoys-data" に .toy の文字列を入れると読める。
  形・リズム・色付けの種類・MIX はホストに出ていない（隠れたつまみ）→ プリセット経由で測った
- 本物の遅れ 45 サンプル。揺れの形はテンポに比例して伸び縮みする（時間の定数ではなく 1 周期の表）。形の表は Digital・DEPTH 100・つまみ中立で 1 kHz を通し、ヒルベルトで包絡を取った
- GROOVE（±0.25）・FEEL（±0.5 周期）・ACCENT（2 拍ごと）・WIDTH（右を先に、左右の混ぜはなし）・DEPTH（直線）は edge.py / pk.py で 1 ms 単位で一致
- しきい値の式は正弦波 −60〜−10 dB のしきい値 4 通りで確認（e = (ピーク×0.8 − T)/0.2 相当）。検出は矩形波で 1.25 倍強く反応（ピーク＋短い戻りで近似、100 Hz〜3 kHz で ±10 %）
- 色付け 7 種類はプリセットの中で数値がそろう組で見分けた（A = Default、B = Zynkpopate、C = Dynamic Chopper、D = 1-16th DoubleDip、E = 1-16th Sqr、F = Glitch Pong、G = Counting Bubbler）。
  stytest.f32（ノイズ −40 dB、100 Hz / 1 kHz / 5 kHz × −30〜+12 dB、−30 → 0 dB の段）で fit/fitde.py（差分進化 → 最小二乗）。gentab.py で TremoloStyleTables.h を作る
- RATE MOD +1 oct の定常の周期は一致。本物は効き始めの数秒、少しだけ遅い（周期 128 → 125 ms）所は再現していない
- 2026-11-10 見直しで確かめて直したこと（最初は推測だった所）:
  - FEEL は全体を同じ時間ずらすのではなく、各回をその回の長さ × 量ずらす（GROOVE −20 %・FEEL −30 % で前の式は 12 % ずれ → 0.4 %）。すき間 0・重なり max はノコギリ波で確認
  - WIDTH は右の時刻を一定量先に（GROOVE と一緒で前の式は 12 % ずれ → 0.3 %）
  - 入力の検出は INPUT の前（INPUT +12 で前の式は 31 % ずれ → 1.6 %）
  - RATE MOD 後の戻り方（4 通りの止め方で、ずれが 8 回 = 1 小節の倍数へ時定数 2.5 秒で寄る）
  - DEPTH 0 で DEPTH MOD −100 は本物も何も起きない（一致）。3 連のリズムでも ACCENT は 2 拍ごと（一致）
  - LOW KEEP の LR4 で高い側を反転していたため分け目で −144 dB の穴（DSP 検査係も見逃し）→ 反転をやめた

## 合わせ込みの到達点（1.0.0、build/tremlab/fullcmp.sh、23 通り）
- 揺れ（Digital、1 kHz −12 dB の包絡の差、平均）: 0.2〜1.7 %（Classic・DEPTH・Sine・GROOVE ±25・FEEL ±・GROOVE と FEEL の組・ACCENT ±・3 連・WIDTH・WIDTH と GROOVE と FEEL の組・
  DEPTH MOD ±・INPUT +12 での DEPTH MOD・97 BPM・Smooth の色付けとの組み合わせ）。矩形・ノコギリの角だけは 1 ms ずれで瞬間的に最大 30 %
- 色付け 7 種類を音楽っぽい素材（和音＋キック・スネア、music.f32）で比べた結果（valstyle.py）: 音量差 −0.1〜+0.5 dB。オクターブごとの差は
  Smooth / Even / Squeeze / Grit / Dense ±0.9 dB 以内、Crunch は 8k +1.4・16k +3.6 dB、Pump は 4k 以上が −4〜−5 dB（ここはまだ合っていない）
- 当てはめは fit/polish.py（stytest＋音楽素材のオクターブ・音量の動き）で仕上げた。Pump はアタック 525 ms・リリース 1.4 ms という逆向きの値に落ち着いた（測った段の動きには合う）

## まだ直していないこと（急がない）
- LOW KEEP の ON / OFF を切り替えた 20 ms の間だけ、素通しと LR4（位相が回る）を混ぜるので分け目の周波数が一瞬へこむ（150 Hz で −25 dB）

## DSP 検査係で直したこと（1.0.0 の push 前、5 回）
- 96k / 192k で 11 Hz の HPF の極が float で 1 に丸まり直流がさまよった → Shelf1 / Biquad を double に
- STYLE 切り替えは前の色付けと 15 ms 重ねる、SHAPE・RHYTHM・パターン・形の変更は 10 ms のつなぎ、INPUT / OUTPUT・LOW KEEP の ON/OFF は 20 ms でならす（LR4 は OFF でも回す）
- パターン・形の受け渡しは SpinLock（オーディオ側は tryLock）。SMOOTH / MODE はオーディオ側で形を作り直す
