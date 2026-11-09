# Deca Satu — 作業メモ（Claude 用）

トラック用のサチュレーター。本人の Mac にある市販サチュレーター（aufx `DEC ` / `SToy`）を測って合わせ込んだ。
2026-11-09 本人「そのまんま同じものを作って、名前は deca satu」。ミックスエンジニア係の判定は「形を変えれば価値あり」→ 本人「その形で」:
AUTO は GAIN MATCH 1 つにまとめる（中に AUTO の曲線を残す）、DSP は DecaSatuDSP.h に分ける（後で Forge Strip に入れられるように）、
つまみは本物どおりで止める、Plugin Link は Gain Stage の LEVEL MATCH だけ。

## ID・コード
- PLUGIN_CODE `Dcst`、Plugin Link の Kind 20（PluginLink.h には未登録。Shared を変えないため。17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient）
- パラメーター（版のヒント 1）: style（Choice A/E/N/T/P）, drive 0..10, punish, lowCut 20..1000 Hz, thump, tone −12..+12 dB, highCut 1k..20k Hz, steep,
  mix 0..100 %, output −24..+6 dB / gainMatch（版 10）, linkGroup, linkMatch

## 音の処理（Source/DecaSatuDSP.h、JUCE なし。数値は自動生成の DecaSatuTables.h）
入口 → LOW CUT（2 次 HPF、Q は表。THUMP で Q 1.0〜1.5）→ TONE の前の分（低域棚＋高域棚、表を補間）→ DRIVE dB（gainA + gainB × drive/10、PUNISH で + A + B × drive）
→ STYLE ごと [ 前の EQ（1 次 HPF・低域棚・山・高域棚）→ 4 倍（FIR ハーフバンド 95 タップ＋23 タップ、直線位相、実際の遅れ 51.75 サンプル・申告 52。88.2/96k は 46.5・申告 46）
  → 歪み（結合コンデンサ cc の 1 次 HPF＋グリッド電流で cc が速く充電＝中心のずれ、gth・gcRate / 遅い中心の戻り sagAmt・sagEnv・sagMs /
     プラス・マイナス別の飽和 lp・kp / ln・kn ＋ slope、bias）→ 後ろの 2 次 LPF → 1 倍へ → 直流カット 2 段 dcHz・dc2Hz（大きい音のあとの「行き過ぎて戻る」揺れ）]
→ TONE の後の分 → HIGH CUT（11 段の表を全部動かして隣の 2 つを混ぜる。ゆるやか = 1 次 LPF＋高域棚＋山、急 = 2 次 LPF × 3）→ OUTPUT（＋GAIN MATCH 中は AUTO の量）→ MIX（原音も同じ FIR を通して位相をそろえる）
- STYLE を変えると 25 ms で前後のスタイルをコサイン曲線で混ぜる（状態は引き継ぐ、混ぜ中の次の切替は待つ）。歪みの表は Engine が 5 スタイル分を最初に作る。
  TONE・LOW CUT・THUMP は 30 ms でなめらかにし、動いている間だけ 1 サンプルごとに係数を作り直す。STEEP は 15 ms で混ぜる。96 kHz 以上は 2 倍、176 kHz 以上は 1 倍
- AUTO（本物は DRIVE に合わせて OUTPUT のつまみを動かすだけ。音には影響しない）: 通常 −N × drive、PUNISH 中 −(Off + Slope × drive)。GAIN MATCH が ON の間だけ extraGainDb に入れる

## 測ったこと（build/decalab、コミットしない）
- 測定ホスト `host`（AVAudioEngine オフライン、v2 API でつまみを入れる）。**つまみを入れたあと RunLoop を 0.3 秒回さないと STYLE が変わらない**。STYLE は 0..4 の整数、ほかは 0..1
- 本物の遅れ 63 サンプル（直線位相）。比べるときは本物を 10.87 サンプル進めると位相が合う（位相のずれを見て決めた値）
- 小さい音: DRIVE は STYLE ごとの決まった dB をまっすぐ足すだけ（A −2.0 + 3.30/目盛り、E −0.2 + 3.32、N −1.5 + 3.10、T +0.9 + 2.40、P −0.8 + 3.05）。音色の形は DRIVE で変わらない
- PUNISH: 約 +14 dB ＋ 目盛りごとに A 0.40 / E 0.48 / N 0.10 / T 0.51 / P 0.75 dB 多め
- LOW CUT・TONE は歪みの前、HIGH CUT は後。TONE の後ろの分（toneShare）は当てはめで E だけ 0.4、ほかは 0（大きい音で効いて見えたのは歪み方の変化）
- 起動直後に直流がゆっくり消える（E は −65 dBFS ほど）。ノイズやハムは足していない
- 当てはめ: initpre.py（小さい音の音色 → 前の EQ）→ fit2.py（6 kHz 以下で波形を比べて歪み・時間の動きを当てる。前の EQ は固定）→ gen_tables.py → evalall.py（答え合わせ）

## 合わせ込みの到達点（1.0.0、build/decalab/evalall.py、当てはめに使っていない別のドラム・声で）
- 1/3 オクターブごとの音色の差（平均 / 最大）: A 0.2/1.0、E 0.2〜1.5/5.9、N 0.4〜0.7/3.2、T 0.6〜1.1/4.3、P 0.7〜1.3/5.4 dB。音量差は ±0.7 dB 以内
  （E・N・T・P は当てはめが音量を下げて逃げたので、出口の倍率を STYLE ごとに +2〜+5.5 dB 補正した）
- 波形の差（6 kHz 以下）: A −18〜−25 dB、N −8〜−20、T −8〜−12、E・P −6〜−8 dB。ミックスエンジニア係の合格ライン（−30 dB）には届いていない
- 本物は大きい音のあと 50〜60 ms ほぼ無音になって急に戻る（グリッドの詰まり）。仕組みは入っているが、当てはめは E・N・T・P で合わせきれていない。
  DRIVE によって頭打ちの高さが上下する（E は D0 で +0.56/−0.84、D5 で +0.25/−0.47）。次に詰めるならここ（モデルの形を 2 段の真空管にする等）
- 傾き slope はマイナスだと強く入れたとき形が折り返すので 0 以上だけ（コードでも slopePos で守る）

## 版
- 1.0.0（2026-11-09）: 最初の版
