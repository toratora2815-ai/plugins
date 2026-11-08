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
  normalise（層ごとに中央値へ ±12 dB まで、soft の中央値 −24 dB RMS）、candidates（同じ弦優先、下げ 0〜3 半音優先、開放の録音は開放だけ）、FLAC で保存
- KitVoice.h … 48 ボイス、Hermite、奏法ピッチ、2 段 1 極ローパス、choke 12 ms、params.lastTake で同じ録音の連続を避ける
- Articulations.h … VoiceParams、奏法、StringAllocator（手の位置 hand、和音は 50 ms 以内に始まった弦を避ける、チャンネル 11〜16 = 画面の試聴で弦指定）、GuitarSynth
- FXChain.h … 前のチャットの仮のもの（ゲート→コンプ→4 倍 OS のドライブ→アンプ→キャビ→リバーブ）。EFFECTS / AMP で作り直す
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

## テスト
`-DAIO_BUILD_RENDER_TEST=ON` で RenderTest。疑似 DI（±20 セント、±6 dB、弦の硬さ指定可）で切り分け・弦・音程・音量・連打・choke・ミュート・状態を確認。
`RenderTest <出力> [弦の硬さ 0〜0.0004]`

## 版
- 0.1.0（2026-11-08）… リポジトリに移す。MY GUITAR ページ（録音 4 欄、弦とフレットの表、PLAYING、奏法）、SCORE ページ（タブ譜、FOLLOW LOGIC / PLAY IN SYNC、DRAG TO LOGIC）、タブ 4 つ（EFFECTS / AMP は仮）、LINK 欄・プリセット欄。
  DSP 検査 1 回目「直してから」→ 確保 0 回・キット差し替え 12 ms フェード・グライドを段ごと・Palm Mute をならす・96k を間引く・decode の範囲チェック → 2 回目「渡してよい」
