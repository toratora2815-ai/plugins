# Meld Verb — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Meld Verb（MeldVerb）… EARLY / PLATE / HALL の 3 つを三角形の BLEND パッドで混ぜるリバーブ。市販の同じ型のリバーブを AU で鳴らして測り、合わせ込んだオリジナル。
エンジンは MeldEngine.h（JUCE なし）: Early（12 個の跳ね返りを TIME の中に並べ、SIZE＝にじみの長さのオールパス、ANGLE＝時間で大きく／小さく＋左右の割合、残りの小さな 4 本 FDN）、
Tank（16 本 FDN。中域の残響時間 = rtMul × TIME〔PLATE 1.32 / HALL 1.25〕、空気の吸収と BALANCE は「1 回あたり一定」の損失、LOWPASS は残響時間の上限〔SIZE で伸びる〕）、
SendSmoother（SMOOTHING。1 ms ごとに 4.9 ms と 2.78 s の包絡を比べ、-3.29 + 0.253 × 比 dB〔-31.9〜+1.9〕を響きへ送る音にかける）、TriEQ（PRE / REVERB EQ、シェルフの GAIN は境目での値）。
Assist.h: LEARN（入力の飛び出た帯域 → PRE EQ のベル、低いゴロゴロ → 70 Hz ローカット）、UNMASK（ドライと響きを 1/6 oct で比べ REVERB EQ を決める。apvts.state の子 "UNMASK" に 100% の値、AMOUNT で入れ直す）。
tools/render_meld.cpp（参考と同じパラメーター番号で書き出し、遅れも 272 サンプルそろえる）、tools/test_meld.cpp（数値テスト）
v1.1.0: SHAPE タブ（響きごとの DELAY〔PRE-DELAY に足す、0〜500 ms〕/ LOW CUT / HIGH CUT〔2 次〕/ WIDTH〔M/S〕、ID {e,p,h}Delay・LoCut・HiCut・Width。標準は素通しで 1.0.1 とビット単位で同じ）、プリセット「Vocal Depth」
- 1.2.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。Dry/Wet 99.5% 以上は効かない。applyPreset の初期化から gainMatch を外した。下の欄を 2 段（footerH 72）に
- 1.2.1（2026-11-12）: オートメーションのジジッを直した（健康診断の click で 12 個、+15〜+51）。MeldEngine.h: Early はオールパス（FracAllpass）と小さな FDN の長さを小数で整数の目標へ 10 ms で近づけ、跳ね返りの並びは 30 ms の混ぜ替え（途中なら終わってから）。Tank は目標を 32 サンプルごとに 30 ms で近づけて applyParams、揺れの深さ・速さ・音量は 1 サンプルずつ、ATTACK のタップは 20 ms の混ぜ替え。響きごとの LOW / HIGH CUT と PRE / REVERB EQ は 4 サンプルごと（EQ はシェルフ・ベルを 0 dB でも区間として残し、形が変わる所だけすぐ切り替える）。prepare では setParams(…, now) ですぐその値。止まっている時の音は前と同じ（Early は起動時の 0.1 秒だけ違う: 前は起動時にも 30 ms 混ぜ替えていた）。「2 つの EARLY を混ぜ替える」案は、音程のある音で打ち消し合って −8 dB へこんだのでやめた
  DSP 検査係（1 回目「直してから」）: ① 追う値が float の丸めで目標の直前で止まり、止まっても係数を作り直し続けていた（48 kHz で +1.4%、192 kHz で +6%、192 kHz の TIME で差 −6 dB）→ 全部「丸めでもう進まないなら目標ちょうど」に ② 響きごとの HIGH / LOW CUT の ON / OFF の境目（既定の 20 kHz から下げる時など）で +52 dB のプチ → いつもフィルターを通し、hpMix / lpMix を 20 ms で混ぜる（OFF でも係数を残す）。Early のにじみも 5 本をいつも通す。EQ の SLOPE の次数の境目のプチ（+60 dB）は前からで、そのまま
