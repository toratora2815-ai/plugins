# My Crystal — 作業メモ（Claude 用）

粒（スプライス）で音程を変え、逆再生もできるエコー。本人の Mac の市販グラニュラー・エコー（aufx `CR  `・`SToy`、/Library/Audio/Plug-Ins/Components）を測って合わせ込んだ。
2026-11-11 本人「クリスタライザー作ろ」（同じ会話で Filter Freak 系 → My Filter の次）。ミックスエンジニア係は「作る価値あり（別プラグイン。Echo Drift に足さない）」、
自作にする意味は VOCAL DUCK と自分のプリセット。LFO 欄は本物にもないので入れていない。

## ID・コード
- PLUGIN_CODE `Mycr`、Plugin Link の Kind 23（PluginLink.h には未登録。22 = My Filter）
- 色付け（StyleChannel の style 1 = Smooth）・4 倍オーバーサンプラー・Biquad は My Tremolo の Source を include（CMakeLists で ../MyTremolo/Source）。
  **My Tremolo の Source（TremoloStyle.h・TremoloStyleTables.h）を変えたら My Crystal もビルド**
- パラメーター（版のヒント 1）: input, output（±24 dB、ウェットだけ）, mix（既定 50）, analog（既定 Analog）, pitch（±3600 セント）, direction（Forward / Reverse、既定 Reverse）,
  splice（0..2050 ms、指数 k=6、既定 500）, spliceNote（1/32〜2 Bars、既定 1/4）, delay（0..2050 ms、既定 100）, delayNote（0・1/64〜1 Bar、既定 1/8）, sync（既定 ON）,
  recycle（0..100 %、既定 21）, threshold（−60..0、既定 −40）, gateDuck（−60..+60 dB、+ = DUCK）, pitchOffset（0..4800 セント、指数 k=8.48、既定 12）,
  spliceOffset（既定 6 %）, delayOffset（既定 2 %）, smoothing（20..2000 ms、既定 20）, lowCut（1..5000 Hz、既定 10）, highCut（500..20000 Hz、既定 10000）,
  attack（0.1..5000 ms、既定 100）, release（既定 500）, duckMode（Output / Recycle / Both）, fbMode（Mixed / Dual / Ping-Pong）, gainMatch（版 10）, linkGroup, linkDuck（0..12 dB、既定 4）
  既定値は本物の初期状態に合わせた（SYNC ON・REVERSE・ANALOG・MIX 50・RECYCLE 0.21 など）
- エディターの TWEAK の開閉は apvts.state の "tweakOpen"

## 測ったこと（build/crylab、コミットしない。th = fflab の測定ホスト、SUB="CR  "。thc = つまみを全部書き出す版）
- つまみは全部ホストに出ている（0..1）。目盛り: Pitch ±3600 直線、PitchOffset 4800·(e^{8.48v}−1)/(e^{8.48}−1)、Splice・Delay 20000·(e^{12.2v}−1)/(e^{12.2}−1) ms（画面は 2050 まで）、
  Smoothing 20+1980v ms、LowCut 5000^v Hz、HighCut 500·40^v Hz、GateDuck (v−0.5)·120 dB、Threshold −60+60v、Attack・Release 0.1·50000^v ms、テンポ同期の SPLICE は round(6v)、DELAY は round(7v)（自作の番号と同じ並び）
- 本物は元の音も効果も 32 サンプル遅らせて申告している。自作は 0（元の音を遅らせない）。比べるときは本物の値から 0.67 ms 引く
- **粒の決まり**（白色雑音を通し、出力を音程比で伸ばすと粒ごとにただの遅れになる → 粒ごとの遅れと利得を最小二乗で読む。build/crylab/gm.py・trans.py・batch1.py）:
  - 遅れ d は 1 サンプルごとに (1 − 音程比) 動く。逆再生は音程比をマイナス（+12 の逆再生なら d が 3 ずつ増える）。音程 0 の順再生は d が動かない（ただのディレイ）
  - 窓は [lo, lo + SPLICE]、元の音からの遅れで lo = max(DELAY, 10.3 ms)。窓の端で SPLICE ちょうど戻す
  - つなぎは直線（利得の和が 1）。d が増える向き（逆再生・音程を下げた順再生）は SMOOTHING の長さ。ただし 1 粒の長さ（SPLICE / |速さ|）の半分まで
  - d が減る向き（音程を上げた順再生）は「今」に追いつく前に終わるよう短い: +12 で 5.2 ms、+24 で 2.8 ms（DELAY があって余裕がある時は SMOOTHING）
  - 右は左より 18.2 ms 手前から始まる（SPLICE OFFSET・DELAY OFFSET が 0 でも左右がずれる）
  - SPLICE OFFSET は右の SPLICE × (1 − %)（100 % で 1/100）、DELAY OFFSET も同じ
  - テンポ同期（120 BPM）: SPLICE は 1/32〜2 Bars、DELAY は 0・1/64〜1 Bar。拍の区切りで粒が頭から始まる
- 結果（自作 vs 本物）: つなぎの長さ ±0.5 ms、粒の位置 ±0.3 ms（遅れの申告分を引いて）。9 通りで確認（build/crylab/cmp1.py）
- **くり返し**: 1 回ごとの倍率 = RECYCLE そのもの（0.5 で −6.0 dB）。LOW CUT・HIGH CUT は 12 dB/oct のバターワースで、くり返しの輪の中（エコー k 個目は k 回通る）。
  Mixed = (L+R)/√2 を両方へ、Dual = 別々、Ping-Pong = 入口は左だけ（自作は左右の平均を左へ。モノなら同じ）、左 → 右はそのまま、右 → 左に RECYCLE
- 補間は直線（くり返すたびに 10 kHz で約 −0.7 dB 暗くなる所、動いている粒の帯域ごとの損失が ±0.1 dB で一致）。エルミートにすると明るすぎた
- **ANALOG**: 色付けは 2 か所。INPUT の後（**入口の音にだけ。くり返しの輪の中には入らない**）と OUTPUT の後。My Tremolo の Smooth（style 1）で奇数倍音・利得が ±0.5 dB（1 kHz、−30〜+6 dB）。
  RECYCLE 0.9 で 0 dB を入れたときのエコーの減り方も ±0.2 dB。偶数倍音は本物の 1 段目が強く 2 段目で打ち消し合う形だが、ふだんの音量では両方同じくらいなので合わせていない
- DIGITAL: 出口で ±0.989 に硬く止める。RECYCLE 100 % でも 0 dBFS で止まる（自作は輪にも安全な天井 +9.5 dBFS）
- MIX: dry = cos(π/2·mix)、wet = sin(π/2·mix)。INPUT・OUTPUT はウェットだけ（説明書どおり）
- **GATE/DUCK**: 量は表示の dB そのもの。入口のピークで判定（すぐ上がる、4.5 ms 待って τ 12.2 ms で下がる）、しきい値の 5 dB 下で切れる（−20 で入って −25 で切れる）。
  下げる量は dB で直線に動く（ATTACK = 全部効くまでの時間、RELEASE = 戻るまでの時間）。下げ始め 1.0 ms（本物 1.5）、戻し始め 26〜40 ms で ±1 ms
- 音楽（build/tremlab/music.f32）で本物の内蔵プリセット 6 個と比べ、音量 ±1 dB、帯域 ±1 dB（多くは ±0.5）。時間ごとの動きは粒の始まる位置で変わるので一致しない（自由なモード）

## 自分で決めたこと
- 元の音は遅らせない（本物は 32 サンプル遅らせて申告）。効果は 2 段目の 4 倍の分 28 サンプル（0.58 ms）遅れて出るが、窓の下端を 0.58 ms 手前にして、元の音からの遅れを本物と同じにした
- Ping-Pong の入口は左右の平均（本物は左だけ）。ステレオの右が消えないように
- 輪の中の安全な天井 v/(1+(|v|/3)^4)^(1/4)。Mixed は同じ信号が左右にあると輪の利得が √2·RECYCLE になる（本物と同じ。右が 18.2 ms ずれているので普通は足し算にならない）
- テンポの代わりのつまみ（本物の Tempo）は入れていない。Logic のテンポが来ないときは 120
- SPLICE / DELAY のつまみは 2050 ms まで（説明書どおり）。テンポ同期の長い音符は 4.1 秒で止める
- 内蔵プリセット 15 個（使いどころ別、音程は ±12・+7・+5 中心。ミックスエンジニア係の案）

## DSP 検査係（2026-11-11）で直したこと
- 輪の中（くり返しに戻す分だけ）に 1 次 5 Hz の直流カット。LOW CUT 1 Hz・PITCH −3600 の順再生・RECYCLE 90〜100 % で直流・超低域が −1.1 dBFS（直流 0.15）まで育っていた → −220 dBFS 以下
- LOW / HIGH CUT を TPT 型の SVF に（応答は同じ）。周波数を log で 25 ms ならし、16 サンプルごとに係数を作り直す。一気に動かしたパチッ −26 → −74 dBFS、ブロック 2048 の掃引 −53 → −96 dBFS
- FEEDBACK MODE の切り替えは前後のつなぎ方を 15 ms 混ぜる（−41 / −36 → −80 / −100 dBFS）
- ANALOG ⇄ DIGITAL は色付けを止めずに回して 10 ms で混ぜる（2 段目の 11 Hz カットの状態が古いまま残らない）。DIGITAL の頭打ちは 4 倍の中で（折り返しを減らす）
- FORWARD ⇄ REVERSE は向き（±1）も 30 ms でならす（−62 → −97 dBFS）
- Logic の Reset（AudioProcessor::reset）で次のブロックの頭に輪を空に。毎サンプルの pow を減らした
- そのままにしたこと: Mixed で RECYCLE 71 % を超えると真ん中の音で輪が育つ（√2 × RECYCLE、本物と同じ。README に注意）。VOCAL DUCK のスライダーで GAIN MATCH が測り直す（Echo Drift と同じ）
- 検査の数値: 1008 通りで NaN・無限大 0 回。192 kHz の最悪の設定でも読む位置は輪の 93.8 %。CPU は 48 kHz で 2.8 %（1 コア）、メモリ 48 kHz で 4 MB・192 kHz で 16 MB。
  直した後の確認は build/crydsp/fixchk.cpp

## 道具
- tools/render_mycrystal.cpp … エンジンでファイルを処理（キーは先頭のコメント）。コンパイル: `clang++ -O2 -std=c++17 -I../../MyTremolo/Source render_mycrystal.cpp`
- build/crylab: c.py（run = 本物、runmy = 自作）、gm.py・trans.py・batch1.py（粒）、fb.py・cmp2.py（くり返し・フィルター）、cmp3.py（ANALOG・GATE/DUCK）、conv.py・music.py（本物のプリセット → 自作の値、音楽で比べる）

## 版
- 1.0.0（2026-11-11）: 最初の版
