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
- 色付けのコンプ（TremoloStyle.h、1.1.0）: compDb=2 = 瞬時の音量 → 縮める量 → 縮める時は倍率（直線）でならし、戻す時は dB でならす（Pump の段の応答の形）。
  compDb=1 = 縮める量を dB でならす、compRms・compRmsMs = 耳を 2 乗の平均に、compGainMs = さらに 1 次でならす、compFb = 耳を VCA の後ろに（今の表では未使用）。
  ニーは 0.001 dB 未満にしない（0/0 の NaN 防止）。hpQ < 0.1 なら出口の低域カットを 1 次に（Grit）
- 時計: SYNC 中で再生中は Logic の ppq にぴったり。RATE MOD が効いている間・終わった後は「Logic の位置 × 今の速さ」へ時定数 2.5 秒で寄せる
  （ずれは lockWrap の倍数を同じとみなす: 4 拍、付点 12 拍、パターンはその長さ。ちょうど半分は前へ）。だから効き始めは数秒だけ速く・遅く回り、終わると小節の頭にそろう。
  ただし速さが少しでも動いた 0.67 ms の区切りの間は寄せない（ゆっくり上げ下げしている間、低い音で量が細かく揺れる間は自走。落ち着いたらすぐ寄せ始める）。
  寄せている最中に再生位置が飛んでも、飛ばさずに寄せ続ける（本物も同じ）。RATE MOD なしで飛んだらすぐ合わせる（10 ms のつなぎ）。止まっている間・SYNC OFF は自走
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
- 入力の量: INPUT のつまみの前の、両チャンネルの |x| の大きいほう → 0.67 ms（48 kHz で 32 サンプル）ごとの最大値 → 下がる時だけ 0.5 ms でならす
  → ATTACK / RELEASE（表示の時間 = 99 % に届く時間、時定数はその 1/4.6）→ しきい値を超えた分 dd
  - DEPTH MOD の量 = dd / 0.25（直線）。RATE MOD の量 = dd / (0.178 + 0.294 dd)（小さい音で 1.4 倍よく効き、dd ≈ 0.25 で 1）。どちらも 1 で止める
  - Gate: しきい値を超えたら 1（ATTACK / RELEASE でならす）。DEPTH・RATE とも同じ量
  - 直流・1 kHz・100〜500 Hz、しきい値 −60 / −40 / −20 dB で本物と比べた（build/tremlab/rm の amtc.py・amtr2.py・dmc.py）
- 速さ: × 2^(RATE MOD × RATE MOD の量)

## 測ったこと（build/tremlab、コミットしない）
- 測定ホスト `th`（AVAudioEngine オフライン、v2 API）。**HostCallbacks（拍・テンポ・再生中）は engine.start() の後に入れないと効かない**
- 内蔵プリセット 142 個（/Users/Shared/Soundtoys/Soundtoys 5/Tremolator/*.toy、中身は暗号化）は ClassInfo の "soundtoys-data" に .toy の文字列を入れると読める。
  形・リズム・色付けの種類・MIX はホストに出ていない（隠れたつまみ）→ プリセット経由で測った
- 本物の遅れ 45 サンプル。揺れの形はテンポに比例して伸び縮みする（時間の定数ではなく 1 周期の表）。形の表は Digital・DEPTH 100・つまみ中立で 1 kHz を通し、ヒルベルトで包絡を取った
- GROOVE（±0.25）・FEEL（±0.5 周期）・ACCENT（2 拍ごと）・WIDTH（右を先に、左右の混ぜはなし）・DEPTH（直線）は edge.py / pk.py で 1 ms 単位で一致
- しきい値の式は正弦波 −60〜−10 dB のしきい値 4 通りで確認（e = (ピーク×0.8 − T)/0.2 相当）。検出は矩形波で 1.25 倍強く反応（ピーク＋短い戻りで近似、100 Hz〜3 kHz で ±10 %）
- 色付け 7 種類はプリセットの中で数値がそろう組で見分けた（A = Default、B = Zynkpopate、C = Dynamic Chopper、D = 1-16th DoubleDip、E = 1-16th Sqr、F = Glitch Pong、G = Counting Bubbler）。
  stytest.f32（ノイズ −40 dB、100 Hz / 1 kHz / 5 kHz × −30〜+12 dB、−30 → 0 dB の段）で fit/fitde.py（差分進化 → 最小二乗）。gentab.py で TremoloStyleTables.h を作る
- RATE MOD（2026-11-10 2 回目、build/tremlab/rm）: 本物は効き始めに「Logic の位置 × 速さ」へ 2.5 秒で寄せていた（前は自走だけだった）。
  入力を静か → 鳴らすの切り替えで、鳴り始めの位置を 0.75〜3 秒・再生開始位置を 0〜802.5 拍で比べ、アクセントで位相を数えて確かめた。
  速さが動いている間は寄せない（4 秒・12 秒・30 秒かけて上げても寄せず、上がり切ってから寄せる。300 Hz・100 Hz の正弦波でも寄せない）。
  量の曲線も直した（前は (ピーク×0.8 − T)/0.2 で、RATE は 6 % 小さく、DEPTH MOD は大きめだった → DEPTH MOD の比較 1.6 % → 0.2 %）
  - 再現していないこと: 再生を始めてすぐ（数秒以内）に RATE MOD が効くと、本物は再生開始位置（小節の中の位置）によって寄せる先が少しずれる（起動直後の癖。0.2 秒ほどで消える
    揺れの位置の乱れもある）。8 秒ほどたてば消える。測定ホストの「次の拍までのサンプル数」「サンプル位置」を正しくしても変わらなかった
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
  Smooth / Even / Squeeze / Grit / Dense ±0.9 dB 以内、Crunch は 8k +1.4・16k +3.6 dB、Pump は 4k 以上が −4〜−5 dB（1.1.0 で直した。下）
- 当てはめは fit/polish.py（stytest＋音楽素材のオクターブ・音量の動き）で仕上げた。Pump はアタック 525 ms・リリース 1.4 ms という逆向きの値に落ち着いた（測った段の動きには合う）

## 1.1.0 で直したこと（2026-11-10、本人「本物と合ってない部分直そうか」、build/tremlab/fix2・rm）
- 色付けを測り直した。新しい測り方: ピンクノイズ・白色ノイズを −48〜−2 dBFS の 8 段（帯域ごとの利得）、2 つの音（大きい音 60 Hz〜10 kHz × 5 段 ＋ 小さい音）、
  音楽素材の 21 ms ごとの帯域別利得（ドラムの立ち上がりでの縮み方）を、前からの stytest・音楽素材に足した（fix2/fit2.py、big.f32 に全部つないで 1 回で通す）。
  fix2/de2.py（差分進化 → 最小二乗）・ls2.py（最小二乗だけ）で当てはめ、gentab2.py で表を作る（best?.json があればそれを使う）
  - 測るときは本物の INPUT・OUTPUT（8・9）を 0.5 にそろえる（Glitch Pong は INPUT が 0.5 でないため、そのままだと小さい音で +4 dB になる）
  - Pump: 本物は「高い音に敏感な耳 ＋ 全帯域を縮める」で、縮み始めは倍率でゆっくり・後半が速い、戻りは dB でまっすぐ。前の当てはめはドラムの頭で 6〜7 dB 縮めすぎて
    4 kHz 以上が −4〜−5 dB だった → compDb=2 で音楽素材の帯域差 ±0.5 dB（差の合計 1296 → 214）
  - Crunch: 本物は時間の動きがほぼない（段の応答が平ら）が、高い音ほど強く潰れる → ごく速いコンプ（ATTACK 0.02 ms・RELEASE 0.025 ms、耳は 3.8 kHz の棚 +26 dB）＋ 2 段の頭打ち。
    8k +1.4・16k +3.6 dB → ±0.3 dB（差の合計 895 → 102）。歪みだけの形（強調 ＋ 頭打ち）では白色ノイズで 5 dB ずれて合わなかった
  - Squeeze・Dense も compDb=2 で当てはめ直し（Dense は白色ノイズの大きい所で 2〜3 dB 縮み足りなかった → 0.4 dB 以内）
  - Grit: 音が止まった直後の低い戻りが、2 次の低域カットで一度沈んで戻っていた → 1 次 5.7 Hz（差の合計 362 → 17.5）
  - 7 種類とも音楽素材で音量差 0.2 dB 以内・オクターブごとの差 ±0.5 dB 以内。100 Hz の 2 倍音（本物 −45〜−55 dB）は Squeeze・Dense・Pump で出ていない（聞こえる大きさではない）
  - 1.0.0 からの音量の変化（ピンクノイズ −26 / −6 dBFS、DEPTH 0）: Squeeze −0.4 / −0.2、Dense −0.6 / −1.7、Crunch −0.4 / −1.4、Grit −0.3 / −0.1、Pump −1.8 / −1.0 dB。
    Smooth・Even・Digital は同じ。保存済みのセッションもこの分だけ変わる
- RATE MOD・入力の量・DEPTH MOD を測り直した（上の「時計」「入力の量」、測ったことの RATE MOD の項）
  - 後ろへ寄せる分が速さを上回ると、しばらく逆向きに回る（本物も同じ。−6 oct ではランプの形が逆に流れるのを確認。−2・−6 oct、ずれ −1.0〜−1.9 拍、開始 11 秒後で本物と差 1 % 前後）
  - DSP 検査係（1.1.0、1 回目「直してから」）: 効いていない間も速さに 2^(量) を使っていて、寄せる先が Logic × 1.0007 になりプツッと飛んだ → 効いていない間は速さ 1 ちょうど。
    寄せている間の休みのつなぎの長さに寄せる分を含める。ニー 0 の守り
## まだ直していないこと（急がない）
- LOW KEEP の ON / OFF を切り替えた 20 ms の間だけ、素通しと LR4（位相が回る）を混ぜるので分け目の周波数が一瞬へこむ（150 Hz で −25 dB）

## DSP 検査係で直したこと（1.0.0 の push 前、5 回）
- 96k / 192k で 11 Hz の HPF の極が float で 1 に丸まり直流がさまよった → Shelf1 / Biquad を double に
- STYLE 切り替えは前の色付けと 15 ms 重ねる、SHAPE・RHYTHM・パターン・形の変更は 10 ms のつなぎ、INPUT / OUTPUT・LOW KEEP の ON/OFF は 20 ms でならす（LR4 は OFF でも回す）
- パターン・形の受け渡しは SpinLock（オーディオ側は tryLock）。SMOOTH / MODE はオーディオ側で形を作り直す

## 1.1.1（2026-11-12）
- DEPTH MOD を 20 ms でなめらかに（depthModS。ほかのつまみと同じ smC）。健康診断の click で Depth Mod +42 が出ていた。止まっている時の音は前と同じ（起動直後の 20 ms だけ、ほかのつまみと同じく近づく）。
  AIO Guitar・My Filter・My Crystal・Retro Retro も同じヘッダーを include しているが、DEPTH MOD を使わない（0 のまま）ので音は同じ
