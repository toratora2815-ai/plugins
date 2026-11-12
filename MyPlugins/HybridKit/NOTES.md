# Hybrid Kit — 作業メモ（Claude 用）

生ドラム（Logic の Drum Kit Designer の Producer Kit の録音）＋ EDM の層のドラム音源（aumu Hbkt Mypl、Plugin Link Kind 27 = 未登録・LINK 欄だけ。26 は Retro Retro）。
2026-11-12 本人「EZdrummer / Superior Drummer をリスペクトしたドラム音源を。生と EDM を混ぜたい、でも生の輪郭は欲しい」。
ミックスエンジニア係は「形を変えれば価値あり」（EZdrummer 風の画面・ミキサーは DKD＋Drummer と MODO DRUM で足りている。
足りないのは 1 本の Drummer トラックで生と EDM を部品ごとに重ね、位相・低音・アタックを自動でそろえること）→ 本人が「生＋EDM のハイブリッド」
「Logic の録音を使う」「DKD の音は好き」「先に WAV で聴き比べ」を選んだ。聴き比べ WAV（Studio Kit、Drummer のループと自作のポップスのビート ×
生 / ＋808 サブ / ＋クラップ / 両方）を渡して本人「OK」→ プラグインにした。

## 録音の読み方（Source/ExsKit.h）
- 表: /Library/Application Support/Logic/Sampler Instruments/03 Drums & Percussion/04 Drum Kit Designer/Drum Kit Designer/<キット>.exs
  録音: /Library/Application Support/Logic/EXS Factory Samples/Drum Kit Designer Consolidated/Drum Kit Designer/*.caf（16 bit 44.1 kHz BE、全部で 25 GB）
- EXS はリトルエンディアンの塊（84 バイトの頭: [3]&0x0F = 種類 0 楽器 / 1 ゾーン / 2 グループ / 3 サンプル / 4 パラメーター、[4] = 大きさ、[16] = "TBOS"、[20] = 名前 64 バイト）
- ゾーン（128 バイト）: [1] 元の鍵盤、[4] 音量 int8、[6][7] 鍵盤の範囲、[9][10] 強さの範囲、[12] 頭の始め、[16] 頭の終わり、[88] グループ、[92] サンプル、
  [116][120] 尾の始め・終わり（DKD だけの欄。頭 0.6〜1.8 秒、尾は近い強さの層どうしで共有）、[124] float の音量補正 dB
- グループ名「キット:部品:奏法:マイク」。部品 Bd1 / Sn1 / TomHi / TomMidHi / TomMidLo / TomLo / HH / CrL / CrR / Rd（Rods は Sn2、タムは「キット:部品:マイク:」の 3 つ）。
  マイク di 近い / d2 近い 2 本目（キックの外・スネアの裏。極性はそろえてある）/ oh / ct かぶり（ステレオ）/ r1 ルーム A（ステレオ）/ r2 ルーム B（モノ）。
  グループの [0] = 音量 int8 dB（Producer Kit の標準のバランス）
- ハイハットの奏法 o1〜o7（開き具合、同じ鍵盤に 7 段重なり。DKD は奏法 ID で選ぶ）・foot（33）・splash（31）。シンバル stop（28 / 29）
- 鍵盤: GM の鍵盤（36〜59）は層が少ない別録り、−24（10〜24）と +48（82〜107）は層の多い別録り。同じ奏法の別テイクとして順番に使う
  （スネア中心 38 / 14 / 86、エッジ 34 / 10 / 82、タム 48 / 24 / 96 など。音色の重心・減衰で分類した。2026-11-12）
- 強さ: 録音は層ごとに音量がそろえてあり、[124] を足すとほぼ同じ大きさ（SoCal Kit v2 は rms+f がどの層も −15.5 dB）。強弱は鳴らす側で付ける。
  曲がりは Drummer のループ（Session Player/Roots Songwriter Drummer - Edgewood.caf の midi と音）からスネア γ≈1.5、キックは [124] の傾きで平らぎみ → γ = 1.25
- 頭 → 尾は 10 ms の等パワーで重ねる（尾は別の録音のことが多い）。つなぎ目の高域の山は最大 +8 dB（=雑音のゆらぎ。プチなし）
- メモリ: CAF は mmap。各ゾーンの頭 4096 フレームだけ attackPool に写す（1 キット 81〜97 MB、読み込み 0.1〜2.5 秒＋EDM の合わせ 0.25 秒）。
  鳴らし始めに Prefetcher が残りのページを裏で触る（全マイクの頭 → 全マイクの尾の順、QOS_CLASS_USER_INTERACTIVE）。
  この Mac はメモリの取り合いが強く、触ったページは 1〜2 分でキャッシュから消える＝ほぼ毎回ディスクから読む前提（先読みなしだと 128 サンプルの枠を超えた）
- Vintage Kit Brushes は奏法の並びが違う（Brush_o1、BrushInner、Stops、splasho4 など）ので 0.1.0 では選べない
- Vintage Kit Rods（2017 年の世代のファイル）は解読（Sn2・「キット:部品:マイク:」・塊の名前が 31 文字に縮んでいるので後ろの長いファイル名を使う）はできるが、
  尾の欄 [116..123] が頭の続きではなく別の大きく鳴っている所を指していて二度鳴りする（尾のある 2539 本中 2503 本で +6 dB 以上、最大 +67.7 dB。DSP 検査係）→ 一覧から外した
- 頭 → 尾: refineSplices（裏のスレッド、キットを出した後に数秒）が層ごとに、全マイクを混ぜた音で頭の終わり 10 ms と尾の始めがいちばん似るずれ（±5 ms）を探す
  （正 = 尾の頭を飛ばす skip、負 = 頭の終わりを削る trim。相関 < 0.3 ならずらさない）。重ね方は a=(1−w)、b=w を 1/√(a²+b²+2ρab) でそろえる（ρ=0 等パワー、1 直線）。
  各マイクの尾の大きさは、頭の終わり 40 ms と尾の始め 40 ms のエネルギー比で −12〜+3 dB。値は LayerFx（atomic）、声は鳴らし始めに写す
  （直す前は、同じ音程の所の打ち消しで −6 dB より深い谷が層の 2.6〜11.7 %、最悪 −18.1 dB だった）

## ノートの割り当て（KitEngine.h buildNoteMap）
36/35 キック、38 スネア（38→14→86 の順）、40 リム、37 サイドスティック、34 エッジ（34→10→82）、32、
41/43 フロア（TomLo）、45 TomMidLo（ないキットは TomLo）、47 TomMidHi、48/50 TomHi、49/55 クラッシュ L、57 クラッシュ R、51/53/59/52 ライド、
28/29 シンバルのストップ（つかむ）、42 ハイハット チップ（開き = CC4、0 で o1）、44 ハイハット 肩（o1。Drummer は 44 をスティックの音として使う＝ループの音で確認）、
46 オープン（o6）、33 フット、31 フットスプラッシュ。ハイハットは開きの小さい音が来たら、それより開いた音を 25 ms（2 以下）/ 60 ms で止める
- 届いた MIDI を開いてから 10 分・5000 行まで ~/Library/Application Support/MyPlugins/Hybrid Kit/midi-log.txt に書く（秒・ppq・状態・data1・data2。2 MB で -old へ）。
  本人が Drummer トラックで鳴らしたら、ここを読んで割り当てを直す（DKD の奏法 ID は AU には来ない）。確かめたら次の版で外す（毎晩の健康診断の試しのノートも混ざる）
- スネアの EDM の層は 38・40 だけ（NoteTarget.edm。サイドスティック 37・エッジ 34・32 には付けない）

## 音の流れ（Source/KitEngine.h、JUCE なし）
- 中の時間は 44.1 kHz。プロセッサーが Resampler.h（片側 32 タップ・512 位相・カイザー β 7.86）でホストのレートへ。ノートを H = 32 個後に置くので遅れ = ceil(32 × out / in)（48 kHz で 35）を申告。
  44.1 kHz のホストでは変換なし・遅れ 0。位置は整数の比（147:160）。1 kHz の残り −109 dB、19 kHz の写り込み −93 dB
- 声 128。部品ごとの上限（キック 3、スネア 6、ハイハット 8、ライド 6、ほか 4）を越えたら一番古いものを 10 ms で消す。同じ層を 2 回続けない、35 % でとなりの層
- 全マイクを同じ位置から鳴らす。モノの近いマイクは、オーバーヘッドの左右の大きさから自動のパン（キック・スネアは 1/4、最大 ±0.8）、ルーム B は真ん中
- つまみ（マイク 4・出口・部品の VOLUME / LENGTH・BLEND 2）は Smoother で 15 ms の直線、1 サンプルずつ（ブロックがノートで細かく切られても続く）。
  render の頭で ramps の配列に作って全部が読む。TUNE・SOUND・NOTE は鳴らし始めの値のまま（サブは KickSound と音程を声に写す）
- 出口に −4 dB（outputTrim。全部の部品を 127 で同時に叩くと +2.8 dBFS だった）
- LENGTH は 90 % から上を「そのまま」へ w² で寄せる（99↔100 % の段差 +11 dB をなくした）
- TUNE: 4 点エルミート、鳴り始めた音はそのまま（次の音から）。LENGTH: 保持 0.03 + 2.5·L² 秒の後 τ = 0.02 + 0.3·L の指数、1.0 は何もしない
- キックの EDM: 部品の束を LR4（100 Hz）で分け、低域 × cos(BLEND·π/2) ＋ サブ × sin(BLEND·π/2)。サブは SubOsc（f = 音 × (1 + sweep·e^(−t/sweepMs))、
  0.6 ms の立ち上がり、−60 dB まで decay 秒）を LR4 の低域側だけ。同時に 1 本（前は 5 ms で消す）、モノ
  analyzeEdm（裏のスレッド、SOUND・NOTE を変えるたび）: 層ごとに、生キックの全マイク（標準バランス）を LR4 の低域にして、
  サブの形との相関が最大の遅れ（鳴り始め ±3 ms）と符号、250 ms のエネルギーの比 → LayerFx の subDelay / subGain。SOUND ごとに +2〜3 dB 大きめ（levelDb）
- スネアの EDM: SnareOsc（Clap = 3 連打＋尾、Big Clap、909 = 185 / 330 Hz の音＋雑音、Snap）。生の鳴り始め + 1.5 ms から、頭を 2 ms 丸める。
  強さ 45 以下は鳴らさない（70 で全部）。大きさは生スネアの全マイクと同じ帯域（Clap 系 400 Hz〜8 kHz、909 は 150 Hz〜8 kHz）・150 ms のエネルギーに合わせる（BLEND 100 % で同じ）
- 録音がない（キットが読めない）ときも EDM の層だけは鳴る

## プロセッサー（Source/PluginProcessor.cpp）
- パラメーター（すべて版 1）: kit（選択 9 個、並びは保存された曲の番号なので変えない・足すなら後ろ）、close / oh / room / bleed（−60〜+12 dB、−60 = 切る）、output、
  <組>Vol / <組>Tune / <組>Len（組 = kick / snare / toms / hihat / cym）、kickBlend / kickSound / kickNote / snareBlend / snareSound / snareEdmTune、linkGroup
- 裏のスレッド Loader: requestedKit が変わったら Kit を作って読み（cancelLoad で途中でやめる。やめたのは失敗扱いにしない）、analyzeEdm → pendingKit に出す → refineSplices。
  持っているキットに戻した（A→B→A）ら状態を Ready / Error に戻す（直す前は「読み込み中」のまま止まり、書き出しが 1 ブロックごとに 41 秒待った）
- オーディオスレッドが取ったら engine.setKit（前のキットは 8 個まで並べて鳴り終わるまで鳴らす）→ takeRetiredKit（そのときの prefetcher.enqueued() を fence に）→ retiredKit
  → メッセージスレッドのタイマーが pendingDeletes にためて、prefetcher.done() が fence に届いてから deleteKit（先読みの途中で munmap すると落ちた）
- 書き出し（isNonRealtime）の間は、loadingKit == requestedKit なら 1 回の読み込みにつき 1 回・最長 30 秒待つ（loadSerial / waitedSerial）。setStateInformation でもすぐ読み始める
- reset() は resetRequested を立てて、次の processBlock で全部止めて変換もリセット
- GAIN MATCH なし（音源）。Plugin Link は LINK 欄だけ

## 試作・テスト（tools/render.cpp、JUCE なし）
- `clang++ -std=c++17 -O2 -o <scratch>/hkrender tools/render.cpp` → `hkrender test`（NaN・ピーク・鳴り止む・ブロックの大きさ・サブの大きさ・輪郭が変わらない・
  ゴーストにクラップなし・ハイハットのチョーク・44.1→48 のひずみと写り込み・48 kHz の流れの大きさと遅れ・TUNE・LENGTH）
- `hkrender demo <出力フォルダ>` … 聴き比べの WAV 8 本（全部同じ LUFS）

## 版
- 0.1.0（2026-11-12）: 最初の版。Producer Kit 8 個、部品 5 組の VOLUME / TUNE / LENGTH、キック・スネアの EDM の層、ミキサー、内蔵プリセット 7 個、MIDI の記録。
  DSP 検査係 1 回目「危ない」（A→B→A で止まる・Rods の二度鳴り・消したキットを先読みが触って落ちる ほか 11 個）→ 全部直して 2 回目「渡してよい」
  （残りの小さい 3 つ＝つなぎ方の計算の途中で選び直したら続きを計算・列が満杯のときだけ止める・空のキットはコンストラクターで用意、も直した。
  test_inspect all は OK 82。キットを読んだ直後の数秒はつなぎ方が前のまま＝書き出しの頭に出うる、EDM の自動の大きさは SOUND / NOTE を替えて 0.2〜0.35 秒で追いつく）
- 次の版でやること（整理係・ミックスエンジニア係）: マルチアウト（キック・スネアだけに「キック迫力」など）を最初に。出口の並びは最初に決めて後から変えない。
  Drummer の割り当てを midi-log で確かめて直したら記録を外す。ミックスの処理（コンプ・歪み）は中に作らない（Fire Box・Drum Transient を後ろに）
