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
- FXChain.h … AMP ページの流れ（4 倍 OS の AMP → CAB → リバーブ → OUTPUT、AMP / CAB の ON/OFF は S 字 15 ms クロスフェード）。中身は Amp.h（下の AMP の項）
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
- Pedals.h: PedalSlot（OFF は計算せず latency 分の遅らせだけ、ON/OFF 15 ms クロスフェード、走り出しで reset）、PedalBoard（順番の入れ替えは 5 ms 下げ → 次のブロックで入れ替え → 5 ms 上げ）
- パラメーター pdOn/pdGain/pdTone/pdLevel、wfOn/wfSustain/wfTone/wfVolume、pedalOrder。範囲と初期値は単体と同じ
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

## テスト
`-DAIO_BUILD_RENDER_TEST=ON` で RenderTest。疑似 DI（±20 セント、±6 dB、弦の硬さ指定可）で切り分け・弦・音程・音量・連打・choke・ミュート・状態を確認。
`RenderTest <出力> [弦の硬さ 0〜0.0004]`

## 版
- 0.1.0（2026-11-08）… リポジトリに移す。MY GUITAR ページ（録音 4 欄、弦とフレットの表、PLAYING、奏法）、SCORE ページ（タブ譜、FOLLOW LOGIC / PLAY IN SYNC、DRAG TO LOGIC）、タブ 4 つ（EFFECTS / AMP は仮）、LINK 欄・プリセット欄。
  DSP 検査 1 回目「直してから」→ 確保 0 回・キット差し替え 12 ms フェード・グライドを段ごと・Palm Mute をならす・96k を間引く・decode の範囲チェック → 2 回目「渡してよい」
- 0.2.0（2026-11-09）… 強さ MEDIUM の層（3 層、ベロシティ 1〜60 / 61〜100 / 101〜127、層は同じ音量でベロシティのカーブ約 20 dB）、
  Cotton Comp、AMP を作り直し（Amp Designer 実測）、録音の尻尾をノイズ +8 dB で切って 80 ms フェード。DSP 検査 3 回
