# プラグイン作りプロジェクト（Logic Pro 用）

このファイルは Claude Code が自動で読む「前提メモ」です。claude.ai のプロジェクト「プラグイン作り」の指示をここに移したものです。

## 使う人の環境
- Mac + Logic Pro のみ。必要なのは AU（.component）だけ。VST3・Windows版は不要
- プログラミングは初心者。手順は1ステップずつ、日本語で、ボタン名やコマンドはそのまま書く
- ビルドは基本 GitHub Actions に任せる（Mac でのローカルビルドは、本人から頼まれたときだけ）
- 本人がやるのは「Artifacts（または Releases）をダウンロード → Logic に入れる」だけにしたい。それ以外は Claude が進める

## GitHub
- リポジトリ: https://github.com/toratora2815-ai/plugins （Public）
- 流れ: 編集 → commit → push → `gh run list` / `gh run watch` でビルド結果まで確認してから報告する
- コミットの作者は Claude にする（`git config user.name Claude` / `git config user.email noreply@anthropic.com`）
- 構成:
  - `.github/workflows/build.yml` … 自動ビルド
    - まず `detect` ジョブ（Linux）が、何をビルドするか決める。Mac は実際にビルドするものの分だけ借りる
    - プラグイン／アプリは `MyPlugins/` の下から自動で見つける（CMakeLists に `juce_add_plugin` ならプラグイン、`juce_add_gui_app` ならアプリ）
    - push 時は、変更があったフォルダのものだけビルドされる（build.yml や CLAUDE.md だけの変更では何もビルドしない）
    - 手動で動かすとき（Actions →「Run workflow」）は、`only` に名前を入れるとそれだけ、空なら全部ビルド
      - Claude から: `gh api -X POST repos/toratora2815-ai/plugins/actions/workflows/build.yml/dispatches -f ref=main -f "inputs[only]=<名前>"`
      - GitHub の Mac（arm64）が混んでビルドが始まらないときは、`-f "inputs[runner]=macos-15-intel"` を付けると別の Mac で動く
    - ビルド結果は Artifacts と、Releases の `latest` に置かれる
  - `MyPlugins/<名前>/` … 各プラグイン（CMakeLists.txt, Source/, README.md）
- 新しいプラグインを足しても build.yml を書き換える必要はない（自動で見つかる）
- `MyPlugins/Shared/` … プラグイン共通の部品（今は Plugin Link）。CMakeLists に `../Shared` を足したものは、Shared を変えると一緒にビルドされる
- 既存プラグインを変えていないときは再ビルド不要（無駄なビルドは走らせない／走ったら止めてよい）
- Claude 側のブラウザは GitHub にログインしていない。ログインが必要な作業は、本人のいつもの Safari / Chrome で開くよう案内する

## 技術の決まり
- JUCE 8.0.15（CMake の FetchContent で自動取得）、C++17
- CMakeLists は既存プラグイン（`MyPlugins/WoolFuzz` など）と同じ形にする
  - `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0、`COPY_AFTER_BUILD` オプション、`juce_generate_juce_header` を使う
  - `COMPANY_NAME "MyPlugins"`、`PLUGIN_MANUFACTURER_CODE Mypl`
  - `PLUGIN_CODE` は4文字でプラグインごとに別にする。使用済み: `Wlfz`, `Nbrv`, `Pchd`, `Blsc`, `Rfrc`, `Hzsp`, `Vltm`, `Fdpl`, `Gnst`
  - ビルドは `<名前>_AU` ターゲットだけ（arm64;x86_64 ユニバーサル、`codesign --sign -` のアドホック署名）
- 歪み系は4倍オーバーサンプリング、パラメーターは APVTS ＋ SmoothedValue でスムージング
- 実在製品の名前・ロゴ・見た目は使わず、音のキャラクターを参考にしたオリジナルとして作る
- 渡す前に、コンパイルが通るかと DSP の数値テスト（発振しないか、歪み量、減衰時間など）を確認する
- 各プラグインに日本語の README（つまみの説明、おすすめ設定）を付ける

## インストール手順（Logic）
1. Actions の実行結果ページ下の「Artifacts」から「<名前>-macOS」をダウンロードして解凍
   （または Releases の「latest」から `<名前>.zip`）
2. ターミナルで:
   ```
   cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
   ```
3. Logic を起動 → Audio Units → MyPlugins
   出ないときは「Logic Pro → 設定 → プラグインマネージャ」で再スキャン
- Artifacts は90日で消える。消えたら Actions →「Build plugins」→「Run workflow」で、`only` に名前を入れてビルドし直す

## 作ったもの
- Wool Fuzz（WoolFuzz）… ビッグマフ系ファズ。SUSTAIN / TONE / VOLUME
- Nebula Reverb（Nebula）… シネマティックリバーブ。MIX / DECAY / SIZE / PRE-DELAY / DAMPING / MOD / SHIMMER / WIDTH / LOW CUT / FREEZE
- Porch Drive（PorchDrive）… BD-2系ブルースドライブ。LEVEL / TONE / GAIN
- Balance Scope（BalanceScope）… 帯域バランス解析。ジャンル別目標カーブ、4帯域メーター、RESET
  v1.1.0: 参考曲のカーブを CAPTURE して比べる（REF A/B/C、apvts.state の子 "REFS" に保存、DIFF 表示）、ラウドネス（INTEGRATED/SHORT-TERM/MOMENTARY/PEAK/PLR、LoudnessMeter.h）、Custom の目標カーブ（ID customTilt / customLow）
  v1.2.0: Plugin Link で「出すぎの帯域」を送る（BandMath.h の findExcess。平均 3 秒以上、150 Hz〜12 kHz、REF と比べているときは REF 基準）
- Reference（Reference）… 自分の曲と参照曲の聴き比べプラグイン（ファイル／ライブ入力、音量自動合わせ）
- Horizon Spread（HorizonSpread）… ステレオ感を出す手法の全部入り。Mid は触らず Side だけ加工（モノラル互換）。
  第1段階 SHAPE ページ: WIDTH 3帯域＋AUTO / AIR / SIDE DRIVE / TRANSIENT、SAFETY: BASS MONO / GUARD、MONO CHECK、ベクトルスコープ。
  CREATE ページ（v1.1.0）: SHIFT（SAFE/CLASSIC）/ DOUBLE / BANDS（ALT/TILT）/ HAZE（CLOUD/SMOOTH）/ MOTION。
  順番は「生み出す → 整える」で固定。MIX と GUARD の基準は CREATE 前の入力
  v1.2.0: AUTO GAIN（標準 ON）、SIDE SOLO、内蔵プリセット11個、帯域ごとの広さメーター、MOTION のテンポ同期、
  SHIFT CLASSIC の L/R 選択、DYNAMIC（DYNAMICS タイル）。量のつまみはどのタイルでも右側に置く
  v1.3.0: Plugin Link で Fader Pilot がボーカルを上げている間 SPREAD を下げる（ID linkDuck、+6 dB・100% で半分。プリセットで変わらない）
- Velvet Tamer（VelvetTamer）… レゾナンス抑制（耳に痛い響きを鳴った瞬間だけ自動で下げる）。STFT で「まわりより飛び出た所」を検出。
  DEPTH / SHARPNESS / SELECTIVITY / ATTACK / RELEASE / FOCUS LOW・HIGH / MIX / OUTPUT / DELTA / LINK。遅延 2048 サンプル（48k）。
  v1.1.0: グラフ上で FOCUS をドラッグ、WEIGHT（7 点固定周波数の効きの強弱カーブ、ID weight0〜6）、プリセットメニュー
  v1.2.0: LOW LATENCY（ID lowlatency、遅延 512 サンプル。エンジンを 2 つ用意して切り替え）、SIDECHAIN（ID sidechain、外部サイドチェーンの音で下げる帯域を決める）
  v1.3.0: Plugin Link で Balance Scope の出すぎ帯域へ FOCUS を自動で（applyLinkFocus、約 0.8 秒で追う。つまみは動かさない）
- Fader Pilot（FaderPilot）… ボーカルの音量を自動でフェーダーを上げ下げして揃える（遅延ゼロ）。TARGET / FLOOR / RANGE / SPEED / OUTPUT、
  AUTO RIDE（OFF で Ride パラメーター＝オートメーションをそのまま使う）、WRITE（再生中に Ride へ書き込み、Logic の Latch/Touch で記録）、
  FOLLOW MUSIC（サイドチェーンのオケの音量＋OFFSET を目標に）、プリセット8個
  v1.1.0: Plugin Link でフェーダー位置（rideDb）と声が鳴っているか（active）を送る
- Gain Stage（GainStage）… トラックの入口の音量をワンクリックでそろえる（遅延ゼロ）。LEARN（鳴っている所だけ 5 秒測り、TARGET になる GAIN を 1 回だけ入れる。
  -60 dB より小さい 100 ms は数えない）、TARGET / RMS・PEAK / Ø POLARITY / MONO / SWAP L/R、IN/OUT メーター。結果は apvts.state の "learnInfo"
- Reference Monitor（ReferenceMonitor）… 聴き比べ用の単体 Mac アプリ（macOS 14.2 以降、プロセスタップで Logic とミュージックの音を受け取る）。
  v1.1.0: LISTEN AS（PHONE / LAPTOP / EARBUDS / BT SPEAKER / CAR / CLUB の聞こえ方、ピンクノイズで音量を自動補正）、
  LEVEL に SPOTIFY / APPLE / YOUTUBE の音量正規化、メーターに各サービスで何 dB 上げ下げされるか
  v1.2.0: 幅 720〜（MacBook 画面の半分）に収まる縦並びレイアウト。左の参照曲リストは上の REF メニューに
  v1.3.3: ウィンドウの位置と大きさを覚える（library.json の settings "window"、動かして 0.8 秒後と終了時に保存）
  v1.4.0: グラフを TONE / WIDTH で切替（W キー、WIDTH＝低・中・高のサイドの割合を MIX と REF で比較。Spectrum.h がサイドも測る）、SNAP（N キー、今のミックスを覚えて点線で比較。右クリックで消す）、VOLUME と REF TRIM は上下ドラッグ／ホイールの数字の箱にして、STEREO と一緒に出力の行へ（下は3段）。リモート命令 "width" / "snap" / "snapClear"
  v1.4.2: + CUE は名前を聞かずにすぐ追加（名前は空、表示は「CUE 番号」。前の版の「Cue 3」も番号扱い）。キューのボタンをダブルクリックで名前変更
  v1.5.0: PUNCH 表示（Punch.h、帯域ごとの 3 秒クレスト dB）、AVG（何曲かの平均の音色と比べる。settings "avgCurves"、ファイルは曲全体を解析、ライブは 15 秒以上の平均）、LISTEN AS と SPOTIFY/APPLE/YOUTUBE は MORE メニューへ（LEVEL は OFF/MATCH だけ、配信サービスのメーター行はそのとき以外隠す）、下は2段で幅は固定、Apple Music の曲は流れた所のモーメンタリー LUFS で音量の形を描く
  v1.6.0: LOW 表示（20〜250 Hz を拡大。LowEnd.h の LowAnalyser が 400 Hz で切って約 6 kHz に間引き、4096 点 FFT で約 1.5 Hz きざみ。自分の曲は 100 Hz〜8 kHz の音量差で REF にそろえて描く。SUB / BASS / LOW-MID の差、30〜150 Hz の一番大きい所、120 Hz 以下のサイドの割合）。W キーは 4 つを順番に
- Mix Time Machine（MixTimeMachine）… バウンス履歴と聴き比べの単体 Mac アプリ。~/Music/Logic（と Bounces フォルダ）を見張って新しいバウンスを自動で取り込み、
  FLAC でコピー保存（~/Music/Mix Time Machine）。A/B を音量合わせ・頭のずれ自動合わせで切替、5回のブラインドテスト、何が変わったか（音量・PLR・広がり・音色差）
- Num Deck（NumDeck）… テンキー 1〜9 に Logic の操作を割り当てる単体 Mac アプリ（ゲーム機メニュー風の 3×3 タイル画面）。
  CGEventTap でテンキーを横取り → Logic の初期キーコマンドを送る（アクセシビリティ許可が必要）。3 ページ（/ と * で切替、v1.7.0 までは −/+）、
  Clear（NumLock）で ON/OFF、「Logic が前面のときだけ」（標準 ON）、タイルクリックで割り当て変更・ショートカットを覚えさせる
  v1.1.0: モード（Logic Pro / 業務、クリックでだけ切替）、各 4 ページ。業務はどのアプリでも動く（コピー等・タブ操作・アプリを開く・ページを開く）。
  Chrome の前面タブを AppleScript で登録。仕事のリンク（URL）は公開リポジトリに書かず、本人の設定ファイル（~/Library/Application Support/Num Deck）にだけ入れる
  v1.2.0: 小さい画面（幅 640 / 高さ 440 未満でコンパクト配置、設定から 430×300 に切替）、ウィンドウの位置と大きさを保存
  v1.3.0: Logic Pro モード 4 ページ目「リファレンス」で Reference Monitor を操作（分散通知 com.myplugins.referencemonitor.command、
  object に "ab" / "mix" / "ref" / "play" / "cue1" など。Reference Monitor 1.3.0 の RemoteCommands が受け取る）
  v1.3.1: 1 ページ目の 3（キャプチャ録音）を Reference Monitor の出力 A/B 切替（"out" / "outA" / "outB"）に
  v1.3.2: 1 ページ目の 1（先頭へ）をトラックのフリーズに（⌃⌥⇧⌘F を送る。Logic のキーコマンド「トラックのフリーズを切り替え」に本人が 1 回覚えさせる）
  v1.4.0: テンキー 0 の欄（横長タイル、添字 9、設定キー slot{p}_0）。1 ページ目の 0 は入力モニタリング（⌃⌥⇧⌘I、Logic で 1 回覚えさせる）
  v1.5.0: アプリのタイルは前面ならもう一度押すと隠して開く前のアプリへ戻る。リンクを開く Chrome のアカウントを設定で選べる（Local State を読む、--profile-directory で開く）
  v1.6.0: テンキー . の欄（添字 10、設定キー slot{p}_dot）。1 ページ目の . は低レイテンシモード（⌃⌥⇧⌘L、Logic で 1 回覚えさせる）
  v1.7.0: モード切替キー（最初は Home=115、設定で覚えさせる。特殊キーは NX_SYSDEFINED を 1000+番号で扱う）。全画面アプリも、戻り先アプリを NSWorkspace で開き直して戻る
  v1.8.0: ページ切替を / と * に（− と + は通す）
  v1.9.0: リンクを 2 画面で開く（Slot の split: "chrome" か アプリの場所。Chrome --new-window で開き、アクセシビリティで左右半分に並べる）
  v1.10.0: 2 画面は「ウィンドウ→フルスクリーンのタイル表示→画面左側」を AX で押し、相手選びの画面で右半分の真ん中（幅 0.875, 高さ 0.5）をクリックして全画面分割に。アプリのページの 0 = アプリを全部終了（"!quitAll"）
  v1.11.0: タイルをドラッグで入れ替え（Deck::swapSlots）。2 画面: 全画面のウィンドウは AXFullScreen を外してから並べる、起動待ちは最大 20 秒
  v1.11.1: アプリのタイル: そのアプリが全画面の左右分割中（AXFullScreen なのに画面幅の 9 割未満）なら、分割を解いてそのアプリだけ全画面に
  v1.12.0: リファレンスのページの 0 = TONE / WIDTH 切替（"width"）、SNAP（"snap"）も選べる
  v1.12.1: 前面のアプリのタイルをもう一度押したとき、全画面（1 枚）なら AXFullScreen を外して 1.3 秒後に隠す
  v1.13.0: ページのタブをドラッグで並べ替え、設定の「出すページ」で隠す（設定キー pageOrder / work_pageOrder、例 "0,1,3"。隠したページの割り当ては残る）
  v1.14.0: ON/OFF のキーも覚えさせられる（toggleKey = ToggleLearned(3) + toggleKeyCode）。本人の設定は Home=ON/OFF、Clear(71)=モード切替
  v1.15.0: アプリのページの . = 全画面 ⇔ 半画面（"!fullHalf"、KeyTap::toggleFullScreenOrSplit。分割の相手は AX の窓一覧から探して覚え、戻すときは makeFullScreenSplit）
  v1.16.0: マクロ（Slot type "macro"、steps = key / action / wait / app / url。Deck::MacroRunner がタイマーで 1 行ずつ、キーの間 80 ms。
  Logic Pro モードは KeyTap::bringLogicFront で 1 回だけ前に出し、最後に returnFromLogic。同じキーをもう一度で中止。画面は MacroEditor.cpp の別ウィンドウ）。
  リンクごとの Chrome アカウントと開く場所（LinkPlace: profile / screen（NSScreen.localizedName）/ region = full・left・right・top・bottom。
  KeyTap::openURLPlaced が --new-window で開いて AX で置き、前が全画面ならそちらに戻す）
  v1.17.0: 押した操作の HUD（KeyTap::showHUD、設定 showHud）。2 画面は左右の 2 アプリ以外を一時的に隠してから相手選び、入らなければ HUD で案内。リンクごとに左右のウィンドウを覚えて使い回す（linkWindows）。全部終了で残すアプリ（keepApps、前方一致、最初は Claude と com.apple.logic）。全画面⇔半画面の相手は 6 組まで。設定の書き出し・読み込み
  v1.17.1: 全画面まわりの決め打ちの待ち時間をやめ、waitFor で状態（AXFullScreen・前面のウィンドウ）を確かめてから次へ。2 分割の前に左のウィンドウへキーボードを向け（focusWindow / isFocused）、別のウィンドウを分割しないように。リンクの使い回しは 2 つがお互いと分割されているときだけ「移るだけ」。アプリを前に出すのは bringAppForward（activateWithOptions は断られることがある）
  v1.18.0: 2 画面のリンクは、そのページ（host）のタブがある Chrome のウィンドウを AppleScript で探して使い回す（入れ直し後も見つかる。分割済みならそこへ移るだけ）。全画面（相手を覚えていない）で「.」→ その前に使っていたアプリと半画面（NSWorkspace の切り替え通知で順番を記録）。「いつも手前」のとき Num Deck の全ウィンドウ（メニュー・マクロ画面も）に CanJoinAllSpaces+FullScreenAuxiliary を付け、ほかのアプリの全画面の上にも出す
  v1.18.1: 「.」（全画面 ⇔ 半画面）は、全画面のとき全画面を解除するだけ（2 画面には戻さない。相手の記憶と前のアプリの記録は削除）
  v1.18.2: 全画面の上に出すには、「いつも手前」のあいだ NSApp を Accessory（補助アプリ）にする必要があった（Regular のままだと CanJoinAllSpaces+FullScreenAuxiliary でも出ない）
  v1.19.0: ウィンドウを動かす部分を Windows_mac.mm に分けた（入口は Windows.h の numdeck::win）。メニューバーのアイコン（KeyTap::setStatusMenu: 画面を出す / テンキー ON / いつも手前 / 終了）
  v1.19.1〜1.19.2: 全画面の出し入れは setFullScreenSurely（切り替わったか確かめて頼み直す。アニメーション中だと無視されるため）。
  右がアプリの 2 画面は、先にそのアプリを前に出してそのデスクトップへ移り、左のページがそこに無ければそこに新しく開く（相手選びには今のデスクトップのウィンドウしか出ない）。
  使い回す左が単独の全画面なら先に解除。Chrome への AppleScript は with timeout of 3 seconds。動きの記録は ~/Library/Logs/Num Deck.log
  GitHub の Mac が借りられないときは、build/tools/venv の cmake + Ninja + build/_juce でこの Mac でビルドできる（本人が頼んだとき）
  v1.20.0: 割り当ての分類に「Hum Keys」（Cat::HumKeys、remote が "hk:" で始まるものは sendRemote("humkeys", …)）。ページは足さず、本人が好きなキーに置く。
  Hum Keys が前面のときも「Logic が前面のときだけ」の対象（isRefMonitorFrontmost が humkeys も見る）
  v1.3.3: Logic Pro モードのキー送信は前面アプリに関係なく Logic へ（KeyTap::sendKeyToLogic。Logic は後ろにいる間のキーを受けないので一瞬前に出して送り、150 ms 後に元のアプリへ戻す）。アプリのアイコン（Resources/Icon.png、tools/make-app-icons.swift で描く。Reference Monitor 1.3.2 も同じ）

- Hum Keys（HumKeys）… 鼻歌・口ドラムをその場で MIDI にする単体 Mac アプリ。仮想 MIDI「Hum Keys」（MidiOutput::createNewDevice）で Logic へ。
  MELODY: 12 kHz に間引いて YIN で約 2.7 ms ごとに音程 → KEY / SCALE に合わせる（今の音を少しひいき）、GLUE / HOLD / OCTAVE / VELOCITY / BEND（±2 半音）/ MIDI CH、
  音量のくぼみ→戻りで同じ音を打ち直す。DRUMS: 全体と 3 kHz 以上の包絡の立ち上がりで検出、14 ms の 150 Hz 以下・7 kHz 以上の割合（直前の残りを引く）で
  KICK / SNARE / HAT（36 / 38 / 42）、LEARN（4 回で声の癖を覚える、settings の protoN_k）、SENS。DSP は VoiceEngine.h（JUCE なし、単体テスト可）。
  マイク許可は MicPermission_mac.mm（AVCaptureDevice、許可待ちで画面を止めない）。`--snapshot <フォルダ>` で画面を PNG に書き出す（確認用）
  v1.1.0: 本人の声（低い男声、F1〜A2）で調整: 音程を 5 回の中央値でならす、約 16 ms で 0.4 半音以上動く間は音を変えない、鳴っている間は YIN の判定を甘く（0.35）。
  句の最初は 8 ms で鳴らし、120 ms 以内に高さが変わったら鳴らし直し（記録では kind=1 で最初の音の高さを直す）。遅れ約 56→32 ms。
  TAKE（録り逃し防止）: Engine::capture に「本当に鳴り始めた時刻」付きで MIDI をため、4 秒黙ると ~/Music/Hum Keys に .mid 保存。
  画面下からドラッグで Logic へ（performExternalDragDropOfFiles）、TEMPO（settings "tempo"）で拍に合わせる、フォルダ / NEW
  v1.2.0: 口ドラム 6 音（KICK/SNARE/HAT/OPEN HAT/TOM/CLAP、36/38/42/46/45/39）。特徴は 14 ms の 6 帯域の割合（log10）、最寄りの覚えた音。
  OPEN HAT / TOM / CLAP は LEARN してから（Params::has）。OPEN HAT を覚えるとハイハット系は 45 ms まで見て高音の残り方（protoSus）で開閉を決める。
  settings は dproto{c}_{k} / dsus{c} / dhas{c}（前の "proto" は使わない）。AUTO（KeyFinder.h、Krumhansl で KEY・メジャー/マイナー、OCTAVE は真ん中が 66 付近）、
  AUTO GATE（2 秒の雑音の 9 割点 +10 dB）。Logic 同期: 仮想 MIDI 入力「Hum Keys」で MIDI クロック＋SPP を受ける（SyncClock.h）、
  音のサンプル番号→時刻（mapHostMs/mapClock、入力遅れを引く）→拍。同期テイクは小節頭からの MIDI（ファイル名 " barN"）、Logic 停止で区切る。
  試し聴き（Preview.h、別の AudioDeviceManager で標準出力、簡単なシンセ・GM ドラム）、履歴（HistoryPanel、▶・ドラッグ・ゴミ箱）、
  MINI（M）/ PIN（P、MacSupport_mac.mm の setFloatOnTop＝Num Deck と同じ Accessory＋全 Space）。分散通知 "com.myplugins.humkeys.command" で
  toggle/on/off/melody/drums/mode/new/preview/history/autoKey/autoGate/octUp/octDown/mini/pin/panic/show を受ける

- Plugin Link（MyPlugins/Shared/PluginLink.h・PluginLinkJuce.h）… 自作プラグイン同士の連携。
  ~/Library/Application Support/MyPlugins/PluginLink-v1.bin を mmap した共有メモリの掲示板（64 席、atomic だけで読み書き、3 秒で不在、10 秒で席を回収）。
  各プラグインに ID `linkGroup`（OFF/1〜4）と、画面いちばん下の LinkBar（LINK の選択＋今の様子）。トラック名は updateTrackProperties で伝える。
  今つながっているもの: Balance Scope → Velvet Tamer（FOCUS）、Fader Pilot → Horizon Spread（SPREAD を下げる）。
  新しくつなぐときは Kind と送る数値の番号（namespace bs / fp のように）を PluginLink.h に足す

## Claude からの提案（余った利用枠の活用）
- Claude が自分で「次に作るもの・直すもの」を3つ提案し、本人が「GO」を出したら作る仕組み。指示書は `ideas/PROPOSAL_PROMPT.md`
- 2つの入口:
  - クラウドの定期実行（スケジュールタスク「プラグイン提案」、1日2回）… 提案を `ideas/proposals/` に保存して push、スマホに通知
  - Mac の残量チェック（`tools/usage-watch/`）… 使用率 50% 以下でリセットまで1時間以内なら、提案を `~/Documents/Claudeの提案/` に保存して Mac に通知
- 「GO 2」「GO 1 3」と言われたら: その会話の提案（なければ `ideas/proposals/` か `~/Documents/Claudeの提案/` の一番新しいもの）の
  その番号を、このファイルの決まりどおりに作ってビルドまで確認する。終わったら提案ファイルのその項目に「→ GO 済み（日付）」と書き足す
- 提案の段階ではコードを変えない

## 単体アプリ（.app）を Mac に入れるとき
- `tools/install-app.sh <名前>`（例: `tools/install-app.sh NumDeck`）で、Releases の latest から ~/Applications に入れる
- この Mac には専用の署名「MyPlugins Local Signing」がある（キーチェーン ~/Library/Keychains/myplugins-signing.keychain-db、
  パスワードは ~/.config/myplugins/signing-keychain-pass）。スクリプトがこれで署名し直すので、
  アクセシビリティなどの許可が更新のたびに外れない（アドホック署名のままだと毎回外れる）
- キーチェーン操作はサンドボックスの外で実行する必要がある

## Mac の Claude Code で作業するとき
- このフォルダで作業していれば、ここで直接ビルドもできる（本人が頼んだときだけ）:
  ```
  cmake -S MyPlugins/<名前> -B build/<名前> -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
  cmake --build build/<名前> --config Release --target <名前>_AU
  ```
  （Xcode と CMake が必要。`COPY_AFTER_BUILD` で ~/Library/Audio/Plug-Ins/Components に自動で入る）
- 入ったプラグインは `auval -a` で一覧、`auval -v aufx <PLUGIN_CODE> Mypl` で検査できる
- `build/` はコミットしない
