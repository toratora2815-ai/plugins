# AIO Guitar — 作業メモ（Claude 用）

AU 音源（aumu Aiog Mypl）＋単体アプリ（Standalone、CMakeLists の ALSO_APP で build.yml が AIOGuitar-App.zip も作る）。
claude.ai のチャット（2026-11-08）で途中まで作られ、~/Music/AIOGuitar（JUCE 8.0.4、会社名 Tora Audio）から移した。移したときに会社名・コードを MyPlugins / Mypl に、JUCE を 8.0.15 に。

## 決まったこと（2026-11-08 本人）
- 3 ページ全部入り: MY GUITAR → EFFECTS → AMP の順に 1 ページずつ作って聴いてもらう
- mix-engineer は「EFFECTS / AMP は Logic 標準・自作と被る」と言ったが、本人の判断で全部入り。条件:
  - EFFECTS の Wool Fuzz / Porch Drive は単体と**同じ DSP**を使う（DSP を Shared などに切り出して両方から使う）
  - AMP は Amp Designer 以上を目指す。Amp Designer を測って合わせ込む（Fire Box と同じやり方）、キャビは IR 読み込み＋マイク種類・位置
  - エフェクト・アンプは 1 つずつ OFF にできる
- 合成の弦モデル（Karplus-Strong）は削除（本人「ゴミ」）。Double・弦モデル用のつまみ・EQ 合わせ込みも削除
- 連打対策: 各弦ダウン/アップを録って交互、1 本の弦は 1 音（StringAllocator＋choke）、Humanize
- 録り方: カポ 0/3/6/9/12＋1〜3 弦 15、低い弦 6〜8 秒、本物のミュート（6〜4 弦 × 0/3/6/9）

## 仕組み（Source/）
- SampleKit.h … slice（5 ms 包絡、9 dB の立ち上がり＋直前 0.3 s の最大 −1 dB まで戻ったときだけ新しい音＝うなり対策）、
  measurePitch（YIN 5 窓の中央値 → 基音を ±25 セント 1 セント刻みの DFT で測り直し。弦の硬さで YIN は数セント高く出るため）、
  assignStrings（同じ音程の連続 = 1 本の弦（ダウン/アップ）、音程が下がったら次の位置、弦の並びを標準チューニングに当てはめる。同点は 1 弦終わり（ミュートは 4 弦））、
  normalise（層ごとに中央値へ ±12 dB まで、ピッキングの層は全部 −21 dB RMS にそろえる。下の MEDIUM の項）、candidates（同じ弦優先、下げ 0〜3 半音優先、開放の録音は開放だけ）、FLAC で保存
- KitVoice.h … 48 ボイス、Hermite、奏法ピッチ、2 段 1 極ローパス、choke 12 ms、params.lastTake で同じ録音の連続を避ける
- Articulations.h … VoiceParams、奏法、StringAllocator（手の位置 hand、和音は 50 ms 以内に始まった弦を避ける、チャンネル 11〜16 = 画面の試聴で弦指定）、GuitarSynth
- Rig.h … 音の流れ全体（COMP / DRIVE / FUZZ / AMP を fxOrder の順に → CAB → OUTPUT、下の「並べ替え」の項）
- FXChain.h … AMP の部品（processAmp = 4 倍 OS の AMP、processCabOut = CAB → OUTPUT、AMP / CAB の ON/OFF は S 字 15 ms クロスフェード）。中身は Amp.h（下の AMP の項）
- AmpPad.h … AMP の後ろにペダルがあるときのそろえる量の表（RenderTest --padtable で作る）
- 状態: "AIOG" マジック＋APVTS＋キットの ValueTree（FLAC）。99 音で約 8 MB
- Plugin Link: Kind 17 を PluginLink.h に**まだ足していない**（kAIOGuitarLinkKind）。次に Shared を触るときに kindAIOGuitar = 17 として足す。今は LINK 欄だけ

## SCORE（2026-11-08 本人「アンプルギターみたいに譜面を出して 1 音ずつ指定」「同期」）
- mix-engineer は「プラグインが譜面を持ち続けると二重管理」と言ったが、本人は同期を選んだ。両方入れて切り替え（パラメーター scoreMode）:
  FOLLOW LOGIC … Logic の MIDI を鳴らし、再生中に CaptureFifo で取り込み（パス = 続けて再生した範囲。パスの中で来なかった音は消す）。
    プラグインでの指定（Override：奏法・弦）は (tick, 音程) で ApplyTable に入れ、次の再生から noteOn のフックで当てる。
    Logic の奏法と同じになったら指定は消える（= Logic に入った）。音が消えた指定は「外れた」として残す（ミュートしたリージョンで消えないように）
  PLAY IN SYNC … プラグインの譜面を playhead の ppq に合わせて renderScoreNotes で鳴らす（Ample Guitar の Tab Player の Sync と同じ）。ホストのノートは無視
- PREVIEW … プラグインの中だけで鳴らす（ホストの bpm、なければ 120）
- DRAG TO LOGIC … 最初の音の小節から始まる .mid（~/Music/AIO Guitar/Exports）、奏法はキースイッチ、CH = STRING なら弦をチャンネルに
- 再生開始・ジャンプで奏法を Normal に戻す（前の再生の最後のキースイッチが残らないように）
- 状態は v3: APVTS ＋ キット ＋ SCORE（ノートは 21 バイトずつのバイナリ、Override は子ノード）
- 取り消しは Override の表ごとのコピー（100 手）

## EFFECTS（2026-11-08 本人「エフェクターは一旦ドライブとファズ」「使ってなかったらオフにして軽く」）
- 音の部品は単体と共有: MyPlugins/WoolFuzz/Source/WoolFuzzDSP.h・MyPlugins/PorchDrive/Source/PorchDriveDSP.h（namespace pedal）。
  切り出す前の HEAD と最大差 1.2e-7・遅れ 4 で同じことを確認（scratch の pedaleq）。CMakeLists で ../WoolFuzz/Source と ../PorchDrive/Source を include
  → **Wool Fuzz / Porch Drive を変えたら AIO Guitar もビルドし直す**（build.yml の依存の追加は workflow 権限がなくまだ push できていない）
- Pedals.h: PedalSlot（OFF は計算せず latency 分の遅らせだけ、ON/OFF 15 ms クロスフェード、走り出しで reset）。並べるのは Rig.h
- パラメーター pdOn/pdGain/pdTone/pdLevel、wfOn/wfSustain/wfTone/wfVolume。範囲と初期値は単体と同じ。順番は fxOrder（0.3.0 から。前の pedalOrder は読み替え）
- 休止: 入力が 0.5 s 無音かつ前のブロックの出力ピーク < 1e-5 で EFFECTS と AMP を止めて無音（fxSleeping）。リバーブ最大でも −96 dBFS まで待つ
- AMP と CAB が両方 OFF のときだけ出口でソフトクリップ（−6 dBFS から上を丸めて 0 dBFS を超えない）
- 古い FXChain のゲート・コンプ・ドライブはパラメーターごと外した（fx.compOn=false など固定）。内蔵プリセットもペダルで作り直し
- CPU（48k・256、1 コア）: ペダル OFF 0.63 %、両方 ON 1.4 %、休止 0.05 %。DSP 検査「渡してよい」

## STRUM（2026-11-08 本人の提案 GO、mix-engineer の形）
- Strum.h の Strummer: 同じサンプル位置の 2 音以上 = 和音。predictStrings で弦を予測して、ダウン = 6→1、アップ = 1→上 N 本（UP STRINGS 3/4/全部、標準 4）。
  1 本目は格子の位置、残りを k×SPEED（標準 7 ms、アップは 0.7 倍、強いほど少し速い）＋Humanize ±1.5 ms だけ遅らせる（遅れの申告は増えない）。強さは 1 本ごと −1 dB、アップは −2.5 dB
- 向き: SCORE の Override.strum > Logic の D1/D#1（38/39、ラッチしない、Note.logicStrum に取り込む）> AUTO（Score::autoStrumDir: 表拍ダウン・裏アップ、和音が 16 分の裏にある小節は 16 分の振り。ApplyTable.bar16）
- MIDI のタイミングは書き換えない（mix-engineer: 50 ms の和音の窓・奏法の照合・クオンタイズを壊すため）。遅らせた音は origPpq で Logic の位置として取り込む。アップで鳴らさない弦も onDroppedNoteOn で取り込む
- ダウンはダウンの録音、アップはアップの録音（nextStrumUp）。ブラッシングで全弦 choke。停止で allNotesOff(0, true)、停止・ジャンプでキューを flush
- 手で弾いた和音（音が数 ms ずつ遅れて届く）: 最初の音から 25 ms 以内の音は同じストロークに入れ、前の弦から 1 本分以上あけて鳴らす（Strummer::joinGroup）。
  最初の音は待てない（遅れになる）ので、並びは届いた順。アップの「上 N 本だけ」は和音全体が分からないと決められないので、手弾きの和音では全部鳴る
- パラメーター strumOn（標準 ON）/ strumSpeed / upStrings。SCORE の和音の上に ↓↑（クリックで AUTO→↓→↑→AUTO）。書き出しで D1/D#1
- 録音ガイドも 2026-11-08 本人「明日録音したい」で作った（下）

## RECORD GUIDE（単体アプリだけ、2026-11-08）
- 入力: makeBuses で wrapperType_Standalone のときだけ入力バス（AU は入力なしのまま）。processBlock の頭で buffer.clear() の前に inputTap.push（出力には流さない）。
  単体アプリは入力を消音で始めるので、ガイド開始時に StandalonePluginHolder の消音を外す（#if JucePlugin_Build_Standalone で juce_StandaloneFilterWindow.h を include）。MICROPHONE_PERMISSION_ENABLED
- GuideCore.h: InputTap（2^21 の mono FIFO）、buildSteps（README と同じ順番）、TakeDetector（5 ms の包絡、立ち上がり 9 dB、−45 dB か床 +6 dB が 250 ms で終わり、次の立ち上がりでも区切る）、
  judge（割れ −1 dBFS / 小さすぎ −40 / 2 回弾き = 高域（差分）の 5 ms 包絡で 30〜200 ms 後に +8 dB / 前の弦 = Goertzel で −24 dB より大きければだめ・−32 で警告 / slice で 1 音 / 半音以上ずれ / オクターブ違いは直して警告 / 短すぎ）。
  弦・フレットは順番から決まっているので assignStrings は使わない
- RecordGuide.h: GuidePanel。カポの位置が変わるところで putTakes（キットに入れる）＋ .aioguitar に自動保存。カポ待ちの間の違う音は無視。生の入力は Recordings/*.wav。表の右クリックで startSingle（その弦・フレットのダウン/アップ）
- テスト: 疑似の入力（わざとの失敗 4 種、カポ待ちの雑音）で 66 / 24 / 8 音が通る
- 2026-11-08 改善（DSP 検査で本物に近い音＝うなり・長い T60・ずれた高音で確認）:
  チューニング確認（開放弦 6 本 ±20 セント）、前の弦は −12 dB 超でだめ・−30 dB 超で警告（共鳴）、ハム（50/60 Hz）は警告、
  2 回弾き = 2 回差分の高域で 30〜200 ms に「+10 dB かつ最初の −6 dB 以内」、録音中の「次の音」= 高域のクリック＋30 ms 平均の上昇、
  鳴り終わりの「平ら」は録音開始時の床 +10 dB 以下のときだけ、最初の 60 ms は床を覚えるだけ、ピックのない切れ端はだめ、
  前の弦・新しい音・ハムは ±50 セントの最大で測る（energyAround）。入力は鳴っているチャンネルを自動選択（2 入力のインターフェイス）
- Kit::slice の立ち上がりも同じ考え方に（録音の頭から、30 ms 平均、無音からでなければ 2 回差分のクリックが必要）。長い音が 2〜5 個に分かれていたのが 1 つに。ハムがあるときだけ音程測定用のコピーに 50/60/100 Hz のノッチ
- FXChain: AMP/DRIVE・CAB の ON/OFF を 15 ms クロスフェード、AMP OFF でも遅れは申告どおり（遅らせ線）
- STRUM: 手で弾いた和音（25 ms 以内に届く音）もストロークに（届いた順、間隔は最大 8 ms）

## MEDIUM の層とコンプ（2026-11-09 本人「録音は中くらい足しましょう」「コンプはあった方がいいかもね」、mix-engineer の形）
- MEDIUM: Kit::slotMedium は enum の**最後**（前に保存したキットの番号がずれないように）。画面・ガイドの並びは Kit::kLayerOrder（soft→medium→hard→mute→brush）
  - ベロシティ: 3 層で 1〜60 soft / 61〜100 medium / 101〜127 hard（Kit::layerForVelocity）。SOFT+HARD だけなら今まで通り 0.62、ない層は隣が受け持つ。medium だけでも mute は鳴らない
  - 層は切り替え（クロスフェードしない＝コムフィルター・アタックの二重を避ける）。normalise で soft / medium / hard を**同じ音量**（−21 dB RMS）にそろえ、
    音量はベロシティのカーブだけ・層は音色だけ（層の境目の段差がなくなる、録る日ごとのゲイン差も効かない）。mute / brush は一番弱い層との録音の差を保つ
  - oneLayer（明るさで強弱）は「ピッキングの層が 1 つだけ」のとき（Kit::pickingLayers）
  - 録音ガイド: MEDIUM も 66 音（間引かない）。README は「まず MEDIUM と BRUSH」。強さの目安と「3 層でピック・位置・ピックアップ・ゲインを変えない」を表示。
    judge の警告: MEDIUM が SOFT より 2 dB 未満大きい / HARD より 2 dB 未満小さい、HARD が一つ下の層より 3 dB 未満大きい
- Cotton Comp（CottonCompDSP.h、AIO Guitar だけ）: EFFECTS のいちばん前に固定（⇄ はドライブとファズの間だけ）。OTA 系 1 種類、COMP / LEVEL / BLEND（ccOn/ccComp/ccLevel/ccBlend）
  - 検出は左右の大きいほう、13 ms ホールド（低い E の 1 周期より長い＝リップルの歪みなし）→ 40 ms リリース。ゲインは dB で下げ 6 ms・戻り 120〜420 ms
  - しきい値 −12〜−42 dBFS、比 2〜10、ニー 8 dB、自動メイクアップ（−14 dBFS の音で失う分の 0.9 倍、最大 +20 dB）。−60 dBFS より下は持ち上げを 14 dB で 0 dB に（尻尾のノイズを持ち上げない）
  - 遅れ 0、オーバーサンプリングなし。名前は SUSTAIN にしない（Wool Fuzz と紛らわしい）

## AMP（2026-11-09、本人「アンプもできる限り作って」「Amp Designer を測って」。AmpliTube は無料版なので測らない）
- Amp.h: ampsim::Amp（4 倍 OS の中）= 入力 HP 20 Hz → 真空管段 2〜4 段（triode()：グリッド側やわらか・カットオフ側硬い、grid[] でバイアスが動く、静止点 restOut を引くので無音なら 0）
  → 決めた段の後に voice（モデル固有 5 本）＋ BASS / MID / TREBLE（各 3 本）→ パワー段（fastTanh、電源サグ）→ PRESENCE（3 本）→ 12 kHz LP。Biquad は double（192k プロジェクトの 768 kHz で 20 Hz HP の極が float だと 1.0 になる）
  - モデル切り替えは 5 ms 下げ → 切り替え（静止点から）→ 10 ms 上げ。MASTER はパワー段の押し込み（Amp Designer の MASTER はただの音量だが、こちらは本物寄りに）
  - モデル: American Clean / Tweed / British Chime / British Crunch / Modern High Gain（名前はオリジナル）。つまみの効き方は 3 家族（American = clean・chime、British = tweed・crunch、Modern）
- ampsim::Cab（元のレート）: 箱 7 本＋マイク 4 本＋位置 3 本＋距離 4 本（固定スロット）か、読み込んだ IR（juce::dsp::Convolution、固定 256 ブロック、0.5 秒まで、+9 dB）。
  IR はメッセージスレッドで SpinLock の受け渡し場所へ → オーディオスレッドが loadImpulseResponse（JUCE は別スレッドからの読み込みと処理の交互を禁止）。IR ⇄ 箱は 8 ms 下げて切り替え
  - IR は状態の CABIR 子ノードに float のまま保存。CABIR のない状態を読むと clearIR
- 2026-11-09 本人「Amp Designer は質が低いので、合わせ込みは今できた分だけで終わり。次は AmpliTube を解析してほしい（言ったときに）」。
  → Amp Designer の追加の測定はしない。AmpliTube は無料版だとオフラインで約 −60 dB になるので、Logic の中で測る形になる（本人が買ったら）
- 測定（build/AmpLab、コミットしない）:
  - make_adtest.py … 試験信号（スイープ 6 段階、196/82/659 Hz の正弦 11 段階、ピック 2 種、57 秒）。Logic の ~/Music/Logic/AIO_adtest.wav
  - make_pst.py … Amp Designer の .pst（ヘッダー 0x18 バイト＋float の並び、パラメーター番号は MADSP.framework の Amp Designer.plist）を 57 個作る → プラグインの ▶ で順に読んで ⌘B でオフラインバウンス（48k・32 bit float、メトロノームは切る、Apple Music に入れない）
  - スピーカー番号 0 = キャビなし（アンプ単体の音が測れる）。Amp Designer の BASS 10 などはつまみ同士が影響し合う（BASS を上げると 1 kHz も動く）→ 実測の「0 / 10 での変化」をフィルターで再現
  - adfit.py … 伝達関数は「出力 / 入力」のスペクトル（揃え・窓なし）で測る（IR の窓切りは揃えがずれると壊れる）。合わせ込んで Source/AmpTables.h を書き出す
  - 線形部分の合い: つまみの変化 0.01〜0.3 dB、箱 1〜1.6 dB、マイク 0.6〜1 dB。歪み量は GAIN 範囲で大まかに合わせた（Crunch は Amp Designer のほうが小さい音から少し歪む）
- DSP 検査 3〜4 回目で直したこと: トーンつまみは 20 ms でならし 32 サンプルごとに係数（正午は 3 本とも飛ばす＝CPU 節約）、
  POSITION / DISTANCE は edge = sqrt(pos/10)・far = dist/10 を 40 ms でならし 2 サンプルごとに係数、
  箱・マイク・IR の切り替えは S 字 8 ms 下げ → パートの残りは無音 → 次のパートの頭で切り替え → 8 ms 上げ（係数を先に変えると旧フィルターの続きが出るため）、
  IR は prepare 前に受け取った分を prepare で読み込み、受け取ったら無音で conv を回して getCurrentIRSize() > 1 ＋ 60 ms まで待つ（それまで・IR が消えたら 4x12 Closed）、
  IR モードではマイク・位置・距離の EQ を素通し、休み中は FXChain::applyResting でフェードなしに切り替え、
  juce::Reverb は dry を 2 倍するので dryLevel = 0.5 ×（1 − 0.6 × mix）、入口で |x| ≥ 1e4 も捨てる
- 出口のソフトクリップは常に（−6 dBFS から丸めて 0 dBFS を超えない）。FXChain の入口で NaN / inf のブロックは 0 にして reset。リバーブは標準 0、0 にしてから 0.3 秒で止める
- 前の版の状態（ampChannel・cabMic）は setStateInformation で読み替え（チャンネル 0/1/2 → モデル 0/3/4、箱 0/1/2 → 2/1/0）

## 並べ替え（2026-11-10 本人「エフェクターの順番を自由に。アンプをどこに置くかも。真ん中に置いたらアンプをいじるのがアンプの画面」）
- 本人の決定: CAB はいつも最後（AMP と一緒に動かさない）、内蔵リバーブは外す。mix-engineer「作る価値あり」（条件: CAB 固定・リバーブを外す・AMP の後ろに固定のそろえ）
- fxOrder（AudioParameterChoice、24 通り＝4 つの並べ方、版のヒント 2、オートメーション不可）→ 0.4.0 で fxChain（120 通り）に置き換え。並びの番号は std::next_permutation の順（0 = Comp > Drive > Fuzz > Amp）。
  rig::orderAt / indexOf / moved（1 つ取り出して入れ直す）/ fromOldPedalOrder（前の pedalOrder 1 = 2 番 Comp > Fuzz > Drive > Amp）。
  状態の読み込み（migrateOrder）で pedalOrder → fxOrder。自分のプリセット（PresetBar の XML）にある古い pedalOrder は読み替えない（Shared を触らないため。0.2.0 は 1 日だけ）
- Rig::processChunk: 入口と各チャンクの後で NaN / inf / |x| ≥ 1e4 を見張る（あれば全部 reset）。並びを変えたら S 字 5 ms 下げ → 次のチャンクで入れ替え → 5 ms 上げ（CAB の前で）。
  休み中は Rig::rest で即入れ替え。遅れの合計は並びに関係なく同じ（12 サンプル @48k）
- AMP の後ろにペダルがあるとき: AMP の出口に ampPad::padDb（機種 × GAIN 5 点 × MASTER 5 点の表、双一次補間、−36〜＋12 dB）を掛け、最後のペダルの後で割り戻す。
  15 ms の掛け算型スムージング。表は DI（1 音 −21 dB RMS の単音＋ダウンストロークの和音）を CAB なしで通した RMS の差。AMP の出口は DI より小さいことが多い（＋の値が多い）
- 画面: EffectsPage.h（MovableBlock = 動かせる箱、PedalPanel・AmpBlock。箱をドラッグ（6 px 以上動いたら）→ 一番近い場所に入れ直し、◀ ▶ で 1 つずつ。
  AMP の箱を押す・CAB の箱を押す → AMP ページ。幅 240 未満ではペダルのつまみを 2＋1 の三角に）。AmpPage.h のいちばん上に今の並び（AMP を光らせる）と「順番を変える（EFFECTS）▸」
- 内蔵リバーブ（juce::Reverb、revMix / revSize）はパラメーターごと外した。持ち越しの 1 番（休み中にリバーブを変えると頭 10 ms が揺れる）もこれで消えた

## 2026-11-09 の持ち越し（0.3.0 で直した）
- 1. 休み中の REVERB → リバーブごと外した
- 2. 休み中に IR を 2 つ続けて読む → Cab::prepareIR で、受け渡しのたびに settleLeft = 0.3 秒。済んだ後も休み中は無音で回し、getCurrentIRSize が変わるたびに conv.reset（遅れて入った 2 つ目のクロスフェードを切る）。
  ついでに runSilent が 256 サンプルまでしか回していなかったのを、ブロック全部を 256 ずつ回すように
- 3. 内蔵リバーブ → 本人「外す」
- 4. 手弾きのアップストローク → Strummer::joinGroup: 後から来た音を足した和音全体で弦の高い順に上 N 本を決め、外れた弦は「まだ待っている → 鳴らさない（dropped、SCORE には知らせる）」
  「もう鳴っている → その場所で止める（GuitarSynth::chokeMessage = チャンネル 16 の CC 102、値 100＋弦、シンセが 12 ms で choke）」。
  低い弦から来ると、6・5 弦は 5〜20 ms だけ鳴ってから止まる（ピックがかすった感じ）。前の和音の低い弦は戻らない
- ついでに: UP STRINGS の「All」が 5 本までしか鳴らなかった（upStrings = 5）→ 6
- DSP 検査 1 回目「直してから」: choke が「予想した弦」（predictStrings）を止めていた。シンセは鳴らす瞬間に弦を選び直すので、ずれると低い弦が残るか上の弦が切れる
  → STRUM ON の音は、ストラマーが決めた弦（SCORE の弦が優先、Strummer::scoreString）を GuitarSynth::strumStringForNote で渡してシンセがそこで鳴らす
  （待ち中は stringForDelayed）。外から来た ch16 の CC 102 は Strummer::process で捨てる
- DSP 検査 2〜4 回目: 全部の音を強制すると単音のレガートの弦選びとバレーの和音が悪くなった → 形を変えた:
  単音はシンセが選ぶ（0.2.0 と同じ）。手弾き和音の 2 音目が来たら、前のブロックで鳴った音の弦は GuitarSynth::stringOf で読み、同じブロックの単音には予想を渡す。
  和音（emitChord）は SCORE の弦 → 高い音から nearestFree（空き弦）。後から来る音（joinGroup）は 行き先 → 順位付けで止める → 残す音の弦を避けて nearestFree、
  空きがなければアップは「この音より低く、その弦でこの音が弾ける音のうちいちばん低い音」に弦を譲ってもらう（上の音＝メロディーを残す）、ダウンはシンセに任せる。
  4 回目「渡してよい」: 低い弦が残る 0（0.2.0 は 534〜969）、ダウンの 1 音欠けは 0.2.0 より少ない、単音は 0.2.0 と同じ、止まらない音 0
- 前から（0.2.0 から）ある: 手弾きのバレー・ハイポジ和音のダウンで 1 音欠けるのが 1〜2 割（StringAllocator が先読みせず 1 音ずつ決めるため）

## ビビり補正（2026-11-10 本人「弾いて 2 秒後とかに震えてネックに当たって変な音（ブワーン）。鳴ってないバージョンに直して。最初は正常だから、綺麗に減衰したらこうなるのを推測して」）
- mix-engineer「形を変えれば価値あり」: 作り直し（倍音を足して続きを作る）は AMP で持ち上がると雑音の床・うなりが消えてばれる → **増えた分だけ帯域ごとに下げる（上げない）**、
  強いビビりは同じマスのきれいな録音の尾を借りる、見分けは「減り方の線からのはみ出し＋雑音の床 +12 dB」、全体スイッチ 1 つ（既定 ON）＋表の右クリックで 1 音ずつ外す、
  RECORD GUIDE で聴き比べ、直した結果ごと保存（版番号）。本人「係のやり方でやりましょう」
- Source/BuzzFix.h（JUCE なし）: 2048 点 STFT（hop 512）、1/6 オクターブ帯域（2 ビン以上）。
  - detect: 0.2 秒以降、高い帯域（max(1.5 kHz, 3·f0)〜12 kHz）を直前 0.35 秒の線（3 フレーム空けて延長、傾き −1 dB/フレームまで）と比べる。
    候補（合計 +3 dB・40 % の帯域が +6 dB）→ 線を固定して、10 フレーム以内に **2 hop おきの 4 フレーム中 3 つ**（＝ 60 ms 以上続く。1 回のクリックは隣り合う 4 フレームにしか入らない）で
    「75 % の帯域 +6 dB と合計 +4 dB、60 % と +6 dB、50 % と +9 dB のどれか、雑音の床 +12 dB」なら確定。
    同時に、音そのもの（3.2·f0 までの帯域の合計と、f0 の帯域）を同じ窓の線と比べ、0.3 秒以内に合計が −10 dB か f0 が −12 dB 落ちたら「手のミュート・指を離した」＝打ち切り、
    候補の 4 フレーム以内に合計が +4 dB 跳ねたら「弾き直し」＝ 0.3 秒先から探し直す。cleanEnd = 最初のフレームの 2 hop 手前。
    うなりは帯域がばらばらに揺れるので割合がいちばん効く（強いうなりのきれいな音で最も近かったのは 33 %）
  - repair: cleanEnd の前 最大 0.8 秒で帯域ごとに線（新しいほど重い、傾き −3 dB/s〜−2 dB/フレーム）＋余裕（残差の 90 % 点、2〜6 dB）、雑音の床 +3 dB より下げない。
    **下げるのはビビっている間だけ**（高い帯域の 40 % が線＋余裕を 6 dB 超え、合計 3 dB 超えのフレーム）: 最後のビビりから 0.3 秒保って 0.3 秒で 0 dB へ戻す（engage）、
    その先（fixEnd 以降）は 1 サンプルも変えない。すぐ下げて 1.3 dB/フレームで戻す、基音のあたり（3.2·f0 まで）は −6 dB、上は −40 dB まで。
    帯域の dB を log 周波数で直線補間してビンへ、オーバーラップ加算（/1.5）。fixFrom より前も 1 サンプルも変えない。
    直した分（元との差）が直したところの −40 dB 未満なら「直さない」（聞いて分からない）。strong = 直し始め 0.5 秒の高い帯域の平均カット < −15 dB かつ 0.4 秒以上ビビる
  - borrowTail: 同じ f0 比（30 セント以内）でエルミート補間 → 帯域バランス（6 kHz まで、いちばん大きい帯域 −45 dB まで）の近い瞬間（音量差 ±12 dB、近い音量ほど優先）→
    その瞬間の音色差を EQ（−12〜+6 dB、録っていない高域は下げるだけ）→ ±1 周期の相互相関（0.6 未満なら借りない）→ RMS で音量合わせ → 40 ms の S 字クロスフェード
- SampleKit.h: Sample の buzz*（buzzVersion・buzzAt・buzzCut・buzzStrong・buzzBorrowed・buzzTried（試した相手の数）・buzzOff）と fixed。
  findBuzz（読むだけ）/ applyBuzz / checkBuzz（slice の中、SOFT/MEDIUM/HARD だけ。RECORD GUIDE でオクターブを直したら調べ直す）。
  borrowTails はキット単位（publish の中）: 貸すのはきれいな録音か「下げただけ」の録音の fixed（借りたもの・strong は貸さない）、借りた結果をもう一度 detect してビビりがあれば使わない、
  相手の数が前に試したときより増えたらまた試す。
  encode: 子 i = samples[i]（必ず 1 対 1）、bzV / bzAt / bzCut / bzStrong / bzBorrow / bzTried / bzOff と fixflac（直した音の FLAC）。
  **applySlice・clearSlot は encoded を空にする**（main からあったバグ: 同じ音数で読み直すと「子の数が同じ」で古い録音が保存され続けた）。
  decode は調べない。processor の checkOldTakes が bzV のないテイクを loader スレッドで findBuzz → メッセージスレッドで（キットが変わっていなければ）applyBuzz・writeBuzz・publish
  （10 秒の音で 25〜80 ms、200 音で数秒。その間は録ったままで鳴る）
- KitVoice は startNote で `audio = &sample->play (params.fixBuzz)`（VoiceParams.fixBuzz を readParams で毎ブロック。切り替えでキットを差し替えないので鳴っている音は切れない）。
  パラメーター kitBuzz（Fix Buzz、既定 ON、**版のヒント 4** = AU のパラメーター番号のいちばん最後。build/aiotrem/au_order.py と同じ計算で 0.4.0 の 54 個は 1 つも動かない）。内蔵プリセットでは変えない（keep に入れた）
- 聴き比べ: processor の auditionTake / auditionSample / stopAudition。キットと同じ受け渡し（SpinLock、オーディオスレッドでは解放しない、timerCallback で解放）、
  シンセの後・ボリュームの前に足す（EFFECTS を通る）、切り替えは前の音を 5 ms で下げる。表の右クリック（AU でも。項目を選んだときにテイクを探し直す）、
  RECORD GUIDE の「▶ 録ったまま / ▶ 直した音」（まだキットに入っていない最後のビビりの音）
- テスト: tools/BuzzTest.cpp（単体、`clang++ -std=c++17 -O2 -I Source tools/BuzzTest.cpp`）。tools/BuzzSynth.h: 弦モデル（偏波が回って縦の振れが育ち、0.35 秒前から音量が膨らみ、
  フレットに当たると 3.4 kHz・6.1 kHz の響き。barrier で当たり続ける長さ）＋ DSP 検査係の Gen（クリック・ゆっくりした手のミュート・指離し・弾き直し）と beatingString。
  きれいな音 88 個・クリック／ミュート／指離し 26 個を 1 つも直さない、ビビり 9 個を全部見つける・最初の 0.15 秒の高域 +15〜25 dB → +0.6〜3.5 dB、
  短いビビりの後の尾は元の弦と 0.6 dB 以内、借りる 8/9。RenderTest の「== buzz fix ==」（ON/OFF/1 音だけ外す、保存・読み込み、古いキットを裏で、同じ音数の読み直し、鳴っている間の切り替え、強いビビりの尾、聴き比べ）
- DSP 検査 1 回目「直してから」（保存のずれ・ビビり後も下げ続ける・借りた尾のビビり、クリック等の誤検出 23/168、切り替えで音が切れる、古いキットで止まる）→ 全部直した
  （検査係の試験 build/buzzinspect: 誤検出 2/168 = 小さい弾き直しだけ、尾の差 0.0〜1.2 dB）→ 2 回目「渡してよい」。その指摘も直した:
  GuitarKit.content（録音の中身の番号。コピーは引き継ぎ、applySlice / putTakes / clearSlot で新しく）が同じなら裏の検査結果を入れる（Level Match OFF の古い曲を開くと
  replaceState → コピーで editKit が変わり、結果が毎回捨てられていた）。違えば checkOldTakes し直す。
  下げ量の戻りは、まだ線を超えている帯域 1.3 dB/フレーム、超えていない帯域 4 dB/フレーム（2 つ続くクリック・こすれ音の後の高域の下がりが 0.3 秒 → 0.1 秒）。
  f0 の帯域だけ落ちたときは打ち切らず 0.3 秒先から探し直す（打ち切りは 3.2·f0 までの合計が落ちたときだけ）
- まだ（急がない）: 続くクリック・短いこすれ音（30〜200 ms）は「ビビり」として消して 〰 が付く（音は良くなる方向）。基音まで打ち消すほど強い合成のビビり（barrier 0.7）は見つからない（録ったまま鳴る）
- まだ: 判定の基準値は合成音で決めた。本人の本物のビビり音（10 音ほど）ときれいな音（20 音ほど）で確かめ直す（mix-engineer のおすすめ）。
  弾いた瞬間（0.2 秒以内）からのビビりは直せない。ピックが鳴っている弦に軽く触れた（−24 dB の弾き直し）はビビりと見ることがある（下げる／尾を借りるだけで音は悪くならない）

## DRIVE A / B（2026-11-11 本人「ギターリグのエフェクターを測って AIO に移植」「歪みはいくらあってもいい」）
- mix-engineer「形を変えれば価値あり」→ 本人の選択（2026-11-11）: 入れるのは AMP の前に置く足元のペダルだけ（ディレイ・リバーブ・揺れものは後ろに挿す）、
  1 回目は TS 系・トレブルブースター・RAT 系、アンプは Guitar Rig の新しいアンプと聴き比べ（→ 本人「どっちのアンプも使えるように」= 今の 5 機種を残して足す）、
  AIO だけの新機能は DOUBLE（ペダルの 1 回目の後）→ 2026-11-12 本人「Twin Take を使う」: AIO には作らない（ダブルは AIO の後ろに Twin Take の GUITAR モード。CLAUDE.md「ダブルの新機能は Twin Take に足す」）。2 回目ワウ、3 回目フェイザー／ヴァイブ、箱は 7 個・歪みは 6〜7 機種で打ち止め（mix-engineer）
- **Guitar Rig はこの Mac では無料版（Player）**: ~/Library/Preferences/com.native-instruments.Guitar Rig 7.plist の lic が "free"。Pro の部品（Cat・Treble Booster・
  Skreamer Deluxe・Jump 以外のアンプ）はラックに入れても黙って捨てられる。動くのは Jump・Matched Cabinet(Pro)・Skreamer・Tube Compressor・Noise Gate・Twin Delay・Studio Reverb など。
  最初に送ったアンプ聴き比べは Guitar Rig 側がキャビだけだった（本人に訂正済み、AIO 側の 5 本は正しい）。Helix Stadium Native（入っている）も測定ホストでは素通し。
  本人「無料版だけ」（2026-11-12）→ ペダルは回路どおり（TS 系だけ Skreamer で答え合わせ）、Guitar Rig のアンプを足すのは見送り（無料版で動くアンプは Jump だけ）。
  道具: build/grlab（gh・pack.py・mkrack.py・ngrr.py・ped/ = Skreamer の 23 通り）、build/hxlab
- 形: 0.5.0 の DRIVE（pd*）と FUZZ（wf*）の箱 = DRIVE A / B。ID はそのまま（表示名だけ Drive A … / Drive B …）、機種は pdModel / wfModel（版のヒント 5、オートメーション不可）。
  並び fxChain の番号も同じ（blkDrive = A、blkFuzz = B）。機種: 0 Porch Drive、1 Wool Fuzz、2 Hearth Drive、3 Brass Boost、4 Barn Dist。初期値 A = Porch、B = Wool
- DriveSlot.h: 5 機種を全部 prepare して選んだ 1 つだけ process。LEVEL は「箱の初期値（A −6、B −12）で各機種の初期値の大きさ」になるよう ずらす
  （Porch の home −6、Wool −12、新 3 機種 0）→ A の Porch・B の Wool は 0.5.0 と 1 サンプルも違わない（テストで 0）。切り替えは 5 ms 下げ → 次のブロックで reset → 10 ms 上げ
- DrivePedals.h（JUCE の Oversampling だけ使う）: 3 機種とも Porch / Wool と同じ 4 倍 IIR（遅れ 4 サンプルで全機種同じ）。voltsPerUnit 1.5（デジタル 1.0 = 1.5 V）
  - HEARTH（TS808）: 回路の値は公開の LTspice ファイル（build/pedcirc/ts808.asc、asc.py でネットリスト化）。クリップ段は帰還ノードを後退オイラー＋ニュートン（1N914: Is 2.52 nA、n 1.752）、
    アース側 4.7k＋0.047 µF の電流で 720 Hz より上だけ増幅。トーン段は 1k/0.22µF＋R9 10k とトーン（20k、220Ω＋0.22µF、帰還 1k）をまとめた 2 次（双一次）。
    TONE は本物だと最後の 1/3 で効くので Rb = 20k·(1−t)^2.2 で広げた。DRIVE は Rf = 51k·10^(0.98k)（dB で等間隔）
    答え合わせ: Guitar Rig 無料版の Skreamer（TS 系の昔のモデル）と小信号の形が 9 通りで 1〜3 dB（低音の切れ・高音の落ち）。歪み始める大きさが合うのが 1.5 V/単位
  - BRASS（ゲルマニウム 1 石）: Ic = Is·exp((Vb−Ve)/nVt)（Is 5 µA、n 1.3、β 80、470k/68k、Re 3.9k＋Ce 47 µF を陽的オイラー、Rc 10k、飽和はなめらかな min）。
    上 +2.4 V（遮断）／下 −5.4 V（飽和）で非対称、強く弾くと Ve が上がって潰れる。RANGE = 入力の HP 80 Hz〜3.2 kHz、BOOST = 入力 −30〜0 dB（出口で半分戻す）
  - BARN（RAT）: 47Ω＋2.2µF / 560Ω＋4.7µF の電流 → Rd（100k、対数）||100 pF → GBW 1 MHz/(1+Rd/43Ω) の 1 次 LP（DIST 最大で 433 Hz から上が落ちる = RAT の荒さ）→ レール ±4 V
    → スルー 0.3 V/µs → 1k＋ダイオード 2 本をニュートン → フィルター 1.5k＋100k·(1−TONE)、3.3 nF（TONE 10 = 開く、本物と逆向きで他のペダルとそろえた）
  - 大きさ: 本人の録音（Brit and Clean #11）を GAIN 5・TONE 5 で通して PORCH の初期値（−13.1 LUFS）に合わせた: Hearth +2.6 dB、Brass −9.6 dB、Barn −0.5 dB（kTrimDb）
  - CPU（48 kHz・1 台）: Porch 0.9 %、Wool 0.6 %、Hearth 2.4 %、Brass 1.7 %、Barn 2.2 %
- 画面: DrivePanel（EffectsPage.h）。「機種 ▾」のポップアップ、色・名前・つまみの名前と並びが機種で変わる（PORCH は LEVEL/TONE/GAIN のまま）。左上に DRIVE A / B
- プリセット（最後に追加）: Hearth Crunch Rhythm、Brass Boost Lead、Barn Dist Grunge
- DSP 検査 1 回目（2026-11-12）「直してから」→ 直した → 2 回目「渡してよい」（表の添字を NaN でも外に出さない 1 行も入れた）: TONE を SmoothedValue 40 ms＋動く間は毎サンプル係数（HEARTH の 2 次、BARN / BRASS は TPT の 1 次）、
  切り替え途中の再切り替えはフェードの位置から続ける・ブロックの途中で切り替える、HEARTH のニュートン最大 8 回、BARN のダイオードは表＋ADAA（Newton なし、CPU 2.2 → 0.7 %）、
  BRASS の静止点を同じ飽和の式で、LEVEL のずらしを l + (home − slotDefault)、Barn Dist Grunge の pdLevel −11。RenderTest ALL PASSED。
  残り（任意）: BARN の GAIN 10 の折り返しは −30〜−38 dB（WOOL と同じくらい）。出どころはオペアンプ段（レールとスルー）で、そこだけ 4 倍の中で 4 回に刻むと約 20 dB 減る（検査係が試した）。
  192 kHz で HEARTH 1 台 17 %、左右同じ音を 2 回計算している（片方だけ計算してコピーすれば半分）検査の道具は build/driveinspect（tonefix・zipper・swfix・converge・alias、ProcInspect tonetick）
- 測定ホストの注意（メモリ measuring-commercial-au にも）: Guitar Rig のラックは入口のゲートが既定 ON、DUMP で読み戻して部品が残っているか必ず確かめる

## まだ直していないこと（DSP 検査 2026-11-10、急がない）
- COMP を AMP の後ろに置いたまま、鳴らしながら AMP の機種・プリセットを替えると 20〜26 ms だけ +4〜5 dB（コンプのピック抜け +5 dB と、
  pad の 15 ms・機種切替の 5 ms 下げ 10 ms 上げ・GAIN の 30 ms のずれ、表の +12 dB 頭打ち）。出口のクリップで 0 dBFS は超えない。pad を 30 ms にすると GAIN 0→10 が悪化したので、時間合わせだけでは直らない
- 0.2.0 で保存した自分のプリセット（.mypreset）の pedalOrder は読み替えない（PresetBar の load は setStateInformation を通らない）

## テスト
`-DAIO_BUILD_RENDER_TEST=ON` で RenderTest。疑似 DI（±20 セント、±6 dB、弦の硬さ指定可）で切り分け・弦・音程・音量・連打・choke・ミュート・状態を確認。
`RenderTest <出力> [弦の硬さ 0〜0.0004]`、`RenderTest --padtable`（AmpPad.h の表を出す、約 1 分）、`RenderTest --amp ...`（AMP の音を WAV で）

## 版
- 0.6.0（2026-11-11）… DRIVE A / B（5 機種から選ぶ: Porch Drive・Wool Fuzz・Hearth Drive・Brass Boost・Barn Dist）。上の「DRIVE A / B」の節
- 0.5.0（2026-11-10）… ビビり補正（Fix Buzz）。上の「ビビり補正」の節
- 0.1.0（2026-11-08）… リポジトリに移す。MY GUITAR ページ（録音 4 欄、弦とフレットの表、PLAYING、奏法）、SCORE ページ（タブ譜、FOLLOW LOGIC / PLAY IN SYNC、DRAG TO LOGIC）、タブ 4 つ（EFFECTS / AMP は仮）、LINK 欄・プリセット欄。
  DSP 検査 1 回目「直してから」→ 確保 0 回・キット差し替え 12 ms フェード・グライドを段ごと・Palm Mute をならす・96k を間引く・decode の範囲チェック → 2 回目「渡してよい」
- 0.4.0（2026-11-10）… EFFECTS に TREM（My Tremolo の小型版）。本人「My Tremolo ができたら愛用ギターにも入れて」→ mix-engineer「形を変えれば価値あり」→ 本人「小型版」。
  並べ替え 5 個目（初期位置 AMP の直後）、DEPTH / RATE（SYNC 中は音符 trRate、OFF・単体アプリは trHz）/ SHAPE（5 種）＋ trSync。色付け・4 倍なし（遅れ 0）。
  Source/TremPedalDSP.h（JUCE なし）が ../MyTremolo/Source の MyTremoloDSP.h（Modulator::gain・kShapes）を使う → **My Tremolo の Source を変えたら AIO Guitar もビルド**。
  並びのパラメーターは新しい ID fxChain（120 通り）。新しいパラメーター 7 個（tr* と fxChain）は版のヒント 3
  （前の fxOrder の 2 より上。1 だと Logic のパラメーター番号が 48 個中 45 個ずれ、前の曲のオートメーションが別のつまみに行く。DSP 検査係が au_order.py で計算）。前の fxOrder（24 通り）・pedalOrder は migrateOrder で「TREM を AMP の直後」に読み替え（rig::fromOrder4）。
  前に保存した自分のプリセット（.mypreset）は fxOrder を持っているので、読むと並びは標準（fxChain の初期値）に戻る。内蔵プリセット「Vintage Amp Trem」（番号がずれないよう、いちばん最後に足した）。
  休止（rest）中は trem.reset()（戻った最初の音が古い揺れの位置から鳴らないように）。SYNC の切り替え・位置の飛び・RATE / SHAPE の変更は 10 ms のつなぎ。
  Rig がブロックを分けて呼ぶとき、Logic の位置を使うのは最初の分だけ（clockFresh）。tools/RenderTest.cpp も fxChain・5 個の並びに直した。
  まだ: PLAY IN SYNC で鳴らしている間、TREM は楽譜の位置ではなく Logic のテンポで自分で回る
  数値テスト build/aiotrem/t.cpp: My Tremolo（Digital）との差 平均 0.2 %、Hz の周期ぴったり、遅れ 0、位置が飛んだ・RATE を変えた所の段差 0.02〜0.06（Square の角 0.13 より小さい）
- 0.3.0（2026-11-10）… EFFECTS で COMP / DRIVE / FUZZ / AMP を好きな順に（Rig.h、AMP の後ろはそろえ AmpPad.h）、CAB はいつも最後、内蔵リバーブを外す、
  持ち越し 4 件（IR 2 つ続けて・手弾きアップストローク・UP STRINGS All）、内蔵プリセット「Even Lead (Drive > Comp)」。mix-engineer「作る価値あり」、DSP 検査 2 回
- 0.2.0（2026-11-09）… 強さ MEDIUM の層（3 層、ベロシティ 1〜60 / 61〜100 / 101〜127、層は同じ音量でベロシティのカーブ約 20 dB）、
  Cotton Comp、AMP を作り直し（Amp Designer 実測）、録音の尻尾をノイズ +8 dB で切って 80 ms フェード。DSP 検査 3 回
