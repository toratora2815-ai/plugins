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
- `MyPlugins/Shared/` … プラグイン共通の部品（Plugin Link、PresetBar＝プリセット保存・取り消し）。CMakeLists に `../Shared` を足したものは、Shared を変えると一緒にビルドされる
- 既存プラグインを変えていないときは再ビルド不要（無駄なビルドは走らせない／走ったら止めてよい）
- Claude 側のブラウザは GitHub にログインしていない。ログインが必要な作業は、本人のいつもの Safari / Chrome で開くよう案内する

## 技術の決まり
- JUCE 8.0.15（CMake の FetchContent で自動取得）、C++17
- CMakeLists は既存プラグイン（`MyPlugins/WoolFuzz` など）と同じ形にする
  - `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0、`COPY_AFTER_BUILD` オプション、`juce_generate_juce_header` を使う
  - `COMPANY_NAME "MyPlugins"`、`PLUGIN_MANUFACTURER_CODE Mypl`
  - `PLUGIN_CODE` は4文字でプラグインごとに別にする。使用済み: `Wlfz`, `Nbrv`, `Pchd`, `Blsc`, `Rfrc`, `Hzsp`, `Vltm`, `Fdpl`, `Gnst`, `Hshf`, `Lmrm`, `Htsc`（廃止）, `Flps`（廃止）, `Smsc`, `Mldv`, `Mdhs`, `Frgs`, `Frgm`, `Eldv`
  - ビルドは `<名前>_AU` ターゲットだけ（arm64;x86_64 ユニバーサル、`codesign --sign -` のアドホック署名）
- 歪み系は4倍オーバーサンプリング、パラメーターは APVTS ＋ SmoothedValue でスムージング
- 実在製品の名前・ロゴ・見た目は使わず、音のキャラクターを参考にしたオリジナルとして作る
- 渡す前に、コンパイルが通るかと DSP の数値テスト（発振しないか、歪み量、減衰時間など）を確認する
- 各プラグインに日本語の README（つまみの説明、おすすめ設定）を付ける
- **どのプラグインにも（今後作るものにも）必ず付ける**（2026-11-06 本人の指示）:
  - 自分のプリセットの保存・読み込みと、取り消し・やり直し … `MyPlugins/Shared/PresetBar.h`。
    プロセッサーに `myp::Presets myPresets { *this, apvts, "<名前>" };`（apvts より後）、エディターに `myp::PresetBar presetBar { processor.myPresets, 色3つ };`。
    自前のプリセット画面があるもの（Lumen Rooms）は `PresetBar (…, true)` で取り消し・やり直しだけ
  - Plugin Link … `plink::Link link { plink::kind<名前>, apvts };`、ID `linkGroup`（makeGroupParameter）、エディターに LinkBar。
    PluginLink.h に Kind を足し、相性のいい相手と「何を送って何に使うか」を決める（プラグインを 1 つにまとめるのではなく、別々のまま連携）
  - 画面のいちばん下に「LINK 欄＋プリセット・取り消し欄」（1 段 40 px か 2 段 72 px。LINK 欄に追加のつまみがあって幅 1000 未満なら 2 段）

## 専門の係（サブエージェント、`.claude/agents/`）
- 言われたとおりに作るだけにしない。おかしいと思ったら作る前に反対意見・別案を言う
- **mix-engineer**（ミックスエンジニア係）… 新しいプラグイン・アプリや大きめの機能追加は、コードを書く前に呼ぶ。
  「やめたほうがいい」「形を変えれば価値あり」と出たら、そのまま作らず本人に結果を伝えて決めてもらう
- **dsp-inspector**（DSP 検査係）… 音の処理を作った／直したら、push する前に呼ぶ。「直してから」「危ない」なら直して、もう一度呼ぶ
- **organizer**（整理係）… 新しいものを足したとき、本人が「整理したい」と言ったとき。提案だけで、消すのは本人が決めてから
- 係の結果は要点を本人に日本語で伝える（係の返事は本人には見えないため）

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
  v1.3.0: Link で上位 2 か所（findExcess2）、JUDGE BY（ID linkWindow、WHOLE / LAST 10 s＝時定数 10 秒の recentPower）、オケの音量 musicDb をブロックごとに送る、
  Velvet Tamer の削り量（グラフに青緑の棒）と Gain Stage の GAIN を受けて表示
- Reference（Reference）… 自分の曲と参照曲の聴き比べプラグイン（ファイル／ライブ入力、音量自動合わせ）
- Horizon Spread（HorizonSpread）… ステレオ感を出す手法の全部入り。Mid は触らず Side だけ加工（モノラル互換）。
  第1段階 SHAPE ページ: WIDTH 3帯域＋AUTO / AIR / SIDE DRIVE / TRANSIENT、SAFETY: BASS MONO / GUARD、MONO CHECK、ベクトルスコープ。
  CREATE ページ（v1.1.0）: SHIFT（SAFE/CLASSIC）/ DOUBLE / BANDS（ALT/TILT）/ HAZE（CLOUD/SMOOTH）/ MOTION。
  順番は「生み出す → 整える」で固定。MIX と GUARD の基準は CREATE 前の入力
  v1.2.0: AUTO GAIN（標準 ON）、SIDE SOLO、内蔵プリセット11個、帯域ごとの広さメーター、MOTION のテンポ同期、
  SHIFT CLASSIC の L/R 選択、DYNAMIC（DYNAMICS タイル）。量のつまみはどのタイルでも右側に置く
  v1.3.0: Plugin Link で Fader Pilot がボーカルを上げている間 SPREAD を下げる（ID linkDuck、+6 dB・100% で半分。プリセットで変わらない）
  v1.3.1: FROM（相手選び）、Plugin Link v2
- Velvet Tamer（VelvetTamer）… 【削除: 2026-11-06 本人の希望。ややこしいので Hush Field に一本化。ソースもリポジトリから消した（git の履歴には残る）。Mac の .component はゴミ箱へ。作り直さない】
- Fader Pilot（FaderPilot）… ボーカルの音量を自動でフェーダーを上げ下げして揃える（遅延ゼロ）。TARGET / FLOOR / RANGE / SPEED / OUTPUT、
  AUTO RIDE（OFF で Ride パラメーター＝オートメーションをそのまま使う）、WRITE（再生中に Ride へ書き込み、Logic の Latch/Touch で記録）、
  FOLLOW MUSIC（サイドチェーンのオケの音量＋OFFSET を目標に）、プリセット8個
  v1.1.0: Plugin Link でフェーダー位置（rideDb）と声が鳴っているか（active）を送る
  v1.2.0: FOLLOW MUSIC で、サイドチェーンに音がなければ同じ LINK の Balance Scope の musicDb を使う（RiderEngine::setLinkMusic）
- Gain Stage（GainStage）… トラックの入口の音量をワンクリックでそろえる（遅延ゼロ）。LEARN（鳴っている所だけ 5 秒測り、TARGET になる GAIN を 1 回だけ入れる。
  -60 dB より小さい 100 ms は数えない）、TARGET / RMS・PEAK / Ø POLARITY / MONO / SWAP L/R、IN/OUT メーター。結果は apvts.state の "learnInfo"
  v1.1.0: Plugin Link で GAIN / TARGET / LEARN 済みかなどを送る（Balance Scope が表示）
- Sample Sculptor（SampleSculptor）… サンプル編集プラグイン（AU 音源 aumu Smsc）。ファイルを落とす → 裏のスレッドで丸ごと作り直す（Render.h、JUCE なし、tools/test_render.cpp）→ 試し聴き（つまみを離すと自動、鍵盤 C3 基準でも）→ 箱をドラッグで Logic へ（毎回 ~/Music/Sample Sculptor に新しい WAV）。
  順番: START/END 切り取り → PITCH（速さごと、エルミート補間、上げるときは FIR で折り返し防止）→ 形（HitEngine.h = 元 Hit Sculptor の ATTACK/SNAP/LENGTH/ROOM の DRY 側）
  → REVERSE SOUND → ROOM の WET（hs::Room、尾の分だけ長く）→ REVERSE SOUND+ROOM（吸い込む響き）→ DRIVE（4 倍、FIR）→ FADE IN/OUT → NORMALIZE(-0.3 dB)+GAIN。apvts.state の "file" に元ファイルの場所
  v1.1.0: Splice / Finder から落とす、EXPORT ボタン、RESULT の波形をつかんで Logic へ（WaveView::onDragOut）、同じ version なら同じファイルを使い回す（lastExportVersion）
  v1.2.0: A/B（abMode、A = 元の START〜END を できた音の RMS にそろえて鳴らす matchGain）、STRETCH（timeStretch = WSOLA、窓 2048・±512 を 1/4 に間引いて探す、切り取りの直後）、
  TONE: LOW CUT / HIGH CUT（バターワース 4 次、前からだけ）・WIDTH（M/S、DRIVE の後）
  （2026-11-06: その場で加工する Hit Sculptor と、逆再生の Flipside を作ったが、本人の意図は「サンプル編集」だったので、これにまとめて 2 つは消した）
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
  v1.19.3: 2 画面の相手選びは、外れたら場所を変えて最大 5 回やり直す（何もない所で閉じた → 並べ直し、違う相手が入った → 2 つとも全画面を解いて並べ直し）。
  開く場所付きのリンク（マクロの中も）も、そのページのタブがある Chrome のウィンドウを使い回す（Google の書類は /d/番号 で見分ける）
  v1.20.0: 割り当ての分類に「Hum Keys」（Cat::HumKeys、remote が "hk:" で始まるものは sendRemote("humkeys", …)）。ページは足さず、本人が好きなキーに置く。
  Hum Keys が前面のときも「Logic が前面のときだけ」の対象（isRefMonitorFrontmost が humkeys も見る）
  v1.21.0: 分類「Idea Catcher」（Cat::IdeaCatcher、remote "ic:" → sendRemote("ideacatcher", …)。toggle / overdub / catch / tap / open / show）。Idea Catcher が前面のときも「Logic が前面のときだけ」の対象
  v1.3.3: Logic Pro モードのキー送信は前面アプリに関係なく Logic へ（KeyTap::sendKeyToLogic。Logic は後ろにいる間のキーを受けないので一瞬前に出して送り、150 ms 後に元のアプリへ戻す）。アプリのアイコン（Resources/Icon.png、tools/make-app-icons.swift で描く。Reference Monitor 1.3.2 も同じ）

  v1.22.0: タイルの種類に「Aside（ひとりごと秘書）」（"as:memo" / "as:undo" / "as:show" → 分散通知 com.myplugins.aside.command）
  v1.23.0: 「トラック」にオートメーションの Latch ⇔ Read（⌃⌥⇧⌘A）/ Latch に（⌃⌥⇧⌘T）/ Read に（⌃⌥⇧⌘R）。最初の割り当てには入れない、Logic に 1 回覚えさせる
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
  v1.3.0: AUTO（mode 2、音ごとに鼻歌か口ドラム。7 kHz〜の割合 feat[5] > -0.7 ならすぐドラム、ほかは 90 ms 聞いて autoVerdict＝声の割合 0.28 以上・高さの幅（2〜8 割点）3 半音未満・
  6 dB 以内なら鼻歌。ドラムは ch10（autoDrumChannel）、CapEvent kind 2）。息よけ（breathGuard、3 kHz〜が -12 dB より多いフレームは声なし）。
  EXPR（expression、CC1 = ビブラートの深さ、CC11 = 声の大きさ）。Metronome.h（CLICK・COUNT-IN、標準出力、耳に届く時刻で拍の目盛り、カウントの小節は負の拍）。
  テイクの拍は takeSource（Logic / クリック / なし）。exportNotes が拍・小節頭・クオンタイズ（QUANTIZE / STRENGTH、出だしだけ寄せる）をまとめ、保存・ドラッグ・試し聴き・画面が同じものを使う。
  TIME SIG（4/4〜12/8、settings "sig"）。MIDI はメロディーとドラムの 2 トラック（ドラムのトラックに "Hum Keys drums"）。
  EDIT（EditPanel、選ぶ・上下・消す・⌘Z・最初の状態。takeLocked = 手直し済み、次の音から新テイク）。命令 auto/click/count/edit/quantize、キー 3/C/R/E/Q。
  Num Deck 1.21.1 にその 5 つ。Idea Catcher 1.1.1 に VoiceEngine.h を写した
  v1.4.0: 「Logic へ」（I キー、macsupport::insertMidiIntoLogic。AX で「読み込む → MIDI…」を押し ⇧⌘G でパス。Logic は再生ヘッド＝小節頭に置くので、
  同期テイクは先に「/」（位置へ移動）で "N 1 1 1"。アクセシビリティ許可が要る）。LOOP（L キー、LENGTH 1/2/4/8 小節、takeLoop 拍。rebuildTakeNotes で周をたたむ、
  1/8 拍早めはその周の頭、カウント中は捨てる。Metronome::setLoop で前の周を ToySynth（Preview.h）で鳴らす。⌫ / 命令 loopUndo で最後の周を消す）。
  命令 toLogic / loop / loopUndo。Num Deck 1.22.1 にその 3 つ

- Hush Field（HushField）… 動的レゾナンス抑制。市販の定番（Mac に入っている本物）を AVAudioUnit で直接動かして測り、振る舞いを合わせたオリジナル。
  HushEngine.h（JUCE なし、vDSP の FFT）: 4096 点を先読み解析 → posToHz の目盛り 1024 点で fine/coarse の山なみ（3 回の箱ならし）、+emph dB/oct で暗めの音を基準 →
  soft/hard・DEPTH・DETAIL の量 → 帯ごとの ATTACK/RELEASE（低/高の TILT 倍率）→ LINK・MAX CUT → 最小位相 FIR（LINEAR PHASE は直線位相）をホップごとに作り FFT でたたみこみ・クロスフェード。
  遅延 2304（LINEAR PHASE 3456、LOW LATENCY 0＝直接たたみこみ）。帯域カーブ 8 本（ID b1〜b8 の used/on/shape/freq/q/gain/slope/focus）、STEREO L/R・M/S、SC、A/B、プリセット 8 個。
  合わせ込みの数値は Tuning（tools/hf_cli.cpp が測定用の CLI）
  v1.1.0: Plugin Link（Balance Scope の出すぎ帯域を linkBands で足す、削り量を 16 帯域で送る）、プリセット保存・取り消し

- Elder Verb（ElderVerb）… 70〜80 年代のデジタルリバーブ機材ふうのリバーブ。この Mac の市販ビンテージリバーブを AU で鳴らして測り、合わせ込んだオリジナル。
  22 モード（Concert Hall〜Vintage Hall、順番は本物の ReverbMode = k/24 の k−1）、COLOR 70s / 80s / NOW、つまみ 15 個は本物と同じ 0..1 の意味なので、
  この Mac の .vpreset をプリセットメニューからそのまま読める。エンジンは ElderEngine.h（JUCE なし）: 入口の拡散 4 段 → 粒のかたまり（ATTACK で形と始まり）→ 4 枝の輪。
  モードごとの性格は Source/Profiles.h（tools/measure の fit.py / fitmod.py → genprof.py で自動生成。作業データは build/ElderVerbFit、AU を鳴らす auhost.swift）。
  70s は出口に 12 kHz の 12 次楕円（VintageFilter.h）＋ループ内 12.7 kHz の 1 次。比べ方は tools/measure/compare.py au
- Meld Verb（MeldVerb）… EARLY / PLATE / HALL の 3 つを三角形の BLEND パッドで混ぜるリバーブ。市販の同じ型のリバーブを AU で鳴らして測り、合わせ込んだオリジナル。
  エンジンは MeldEngine.h（JUCE なし）: Early（12 個の跳ね返りを TIME の中に並べ、SIZE＝にじみの長さのオールパス、ANGLE＝時間で大きく／小さく＋左右の割合、残りの小さな 4 本 FDN）、
  Tank（16 本 FDN。中域の残響時間 = rtMul × TIME〔PLATE 1.32 / HALL 1.25〕、空気の吸収と BALANCE は「1 回あたり一定」の損失、LOWPASS は残響時間の上限〔SIZE で伸びる〕）、
  SendSmoother（SMOOTHING。1 ms ごとに 4.9 ms と 2.78 s の包絡を比べ、-3.29 + 0.253 × 比 dB〔-31.9〜+1.9〕を響きへ送る音にかける）、TriEQ（PRE / REVERB EQ、シェルフの GAIN は境目での値）。
  Assist.h: LEARN（入力の飛び出た帯域 → PRE EQ のベル、低いゴロゴロ → 70 Hz ローカット）、UNMASK（ドライと響きを 1/6 oct で比べ REVERB EQ を決める。apvts.state の子 "UNMASK" に 100% の値、AMOUNT で入れ直す）。
  tools/render_meld.cpp（参考と同じパラメーター番号で書き出し、遅れも 272 サンプルそろえる）、tools/test_meld.cpp（数値テスト）
  v1.1.0: SHAPE タブ（響きごとの DELAY〔PRE-DELAY に足す、0〜500 ms〕/ LOW CUT / HIGH CUT〔2 次〕/ WIDTH〔M/S〕、ID {e,p,h}Delay・LoCut・HiCut・Width。標準は素通しで 1.0.1 とビット単位で同じ）、プリセット「Vocal Depth」

- Mod Host（ModHost）… 好きな AU エフェクトを中に読み込み（JUCE_PLUGINHOST_AU、AuList.cpp が AudioComponentFindNext で一覧、createPluginInstanceAsync）、
  MOD 1〜4（LFO / ENV / SIDECHAIN / RANDOM / STEPS、ModEngine.h は JUCE なし、tools/test_mod.cpp）で中のどのつまみでも動かす。
  1 MOD に 4 か所（ID m{n}a{t} が AMOUNT）、つなぎ先は LEARN（中のつまみを動かす＝audioProcessorParameterChanged）か一覧。
  つないでいる間は 64 サンプルごとに中のプラグインを回し、元の位置（人が動かしたら更新）＋ MOD×AMOUNT を setValue。
  保存は apvts＋STEPS＋PLUGIN（PluginDescription）＋PSTATE（base64）＋ROUTES（番号・名前・元の位置）。保存用の stateLock と音用の hostLock は別
  v1.1.0: 通り道（Chain.h、JUCE なし、tools/test_chain.cpp）: 3 帯域 LR4（SPLIT LOW/HIGH、SEND LOW/MID/HIGH、ID bands）→ DRIVE → 中 → MIX（元の音はレイテンシー分 Delay）→ AUTO GAIN（ID autoGain）→ OUT。
  Mod Host 自身のつまみも MOD の行き先（番号 internalBase=1<<20 ＋ k、保存は IROUTES）。MORPH（STORE A/B、違うつまみを morph で補間、切り替え式は 0.5 で、保存は MORPH の a/b）。
  Analyzer.h: 入口（遅らせた元の音）と出口を M/S で FFT 4096、BANDS ページで帯域ごとの LEVEL 差・WIDTH（サイドの割合）の差とスペクトル

- Plugin Link（MyPlugins/Shared/PluginLink.h・PluginLinkJuce.h）… 自作プラグイン同士の連携。
  ~/Library/Application Support/MyPlugins/PluginLink-v2.bin を mmap した共有メモリの掲示板（64 席・数値 24 個、atomic だけで読み書き、3 秒で不在、10 秒で席を回収）。
  v2 で並びを変えたので v1（PluginLink-v1.bin）の古い版とはつながらない。席の並びを変えるときはファイル名と magic も変える。
  各プラグインに ID `linkGroup`（OFF/1〜4）と、画面いちばん下の LinkBar（LINK の選択＋今の様子）。トラック名は updateTrackProperties で伝える。
  FROM（相手選び、LinkBar::setSources）… apvts.state の "linkFrom" に「種類番号|トラック名」で保存、Endpoint::setPreferred で find がその相手を優先。
  今つながっているもの: Balance Scope → Hush Field（出すぎの帯域にベルを足して強めに処理、FOLLOW）、Balance Scope → Fader Pilot（オケの音量）、
  Fader Pilot → Horizon Spread（SPREAD を下げる）、Hush Field → Balance Scope（削り量の表示。並びは namespace tamer）、Gain Stage → Balance Scope（GAIN の表示）、
  Fader Pilot → Nebula / Lumen Rooms / Elder Verb / Meld Verb（歌っている間リバーブを VOCAL DUCK dB 下げる。plink::VocalDuck、ID linkDuck）、
  Gain Stage → Wool Fuzz / Porch Drive（出口を TARGET にそろえる。plink::LevelMatch、ID linkMatch。RMS モードのときだけ）、
  Reference → Balance Scope（参照曲を聴いている間は測らない。ref::listeningRef）、Fader Pilot / Balance Scope → Mod Host（MOD の種類 LINK）。
  Sample Sculptor は LINK 欄だけ（自然な相手なし）。Kind 3 は前は Velvet Tamer、今は Hush Field
  新しくつなぐときは Kind と送る数値の番号（namespace bs / fp / tamer / gs / ref / out のように）を PluginLink.h に足す

- Idea Catcher（IdeaCatcher）… 思いつきの鼻歌・口ドラムを録って、キーとテンポ入りの Logic プロジェクトにするメニューバー常駐アプリ（LSUIElement、Dock に出ない）。
  録音キー（Carbon の RegisterEventHotKey、最初は ⌃⌥I、アクセシビリティ不要）で録音／停止、黙って 4 秒でも止まる。マイクは録るときだけ開く。
  解析は IdeaAnalysis.h（JUCE なし）: Hum Keys の VoiceEngine.h / KeyFinder.h の写しを録音後にかける（1 回目で音程→キー、2 回目で音階に合わせる、ドラムは Hum Keys の設定 dproto/dhas/dsus を読む）、
  鼻歌らしさ（音程のとれた割合）でメロディー／ドラム、テンポは出だしの自己相関。SMF（タイプ 1、テンポ・4/4・キー、Melody / Drums(ch10) トラック）を自前で書き、
  NSWorkspace で Logic（com.apple.logic10）に開かせる＝新しいプロジェクトになる。保存は ~/Music/Idea Catcher/<日時>/（.mid・Voice.wav・info.json）。
  分散通知 com.myplugins.ideacatcher.command（toggle / start / stop / show / open）。`--snapshot <フォルダ> [<wav>]` で画面を PNG に
  （VoiceEngine.h は Hum Keys の写し（今は 1.3.0）。Hum Keys のエンジンを直したら、ここにも写す）
  v1.1.0: さかのぼり録音（設定 preroll、マイクを開いたまま ring に約 32 秒。押した所から前の声のかたまり＝2 秒黙った所まで、直前 5 秒に声がなければなし。"catch" で直前だけ）、
  タップ（録音中だけ Space を registerExtraHotKey(1,…)、idea::tapGrid）、カウントイン（countIn / countBpm / clickDuring、クリックは出力へ。gridT0 = カウント後の 1 拍目＋入出力の遅れ、gridStep 4）、
  重ね録り（Layer = Result＋kind、Voice 2.wav…、info.json の "layers"。前のレイヤーを effectiveGrid で固定して、1 小節目から小節ループで流しながら録る。テンポ・キー固定）、
  HUD（Hud、mac::makeOverlay、このアプリが前面でないときだけ）、通知（UNUserNotificationCenter、autoOpen OFF のとき。クリックで Logic）、
  声を Logic へ（Voice for Logic.wav を作り、mac::importAudioIntoLogic が AX で ⇧⌘I のメニュー項目を押して ⇧⌘G でパス入力。窓は MIDI のファイル名か 名称未設定 で見分ける。アクセシビリティ許可が要る、設定 voiceToLogic）、
  MIDI に "Key Am" のマーカー。Num Deck 1.21.0 に Cat::IdeaCatcher（remote "ic:…" → sendRemote("ideacatcher", …)）
  v1.2.0: マイクは Mac 本体に固定（設定 builtInMic、mac::builtInMicName が CoreAudio の Built-in で入力のある機器を探す。出力はそのまま）。
  ノイズ除去（NoiseReduce.h、JUCE なし。30 Hz ハイパス→STFT 2048/512、静かな 15% の平均をノイズに、ならしたパワーでウィーナー風に下げる。設定 noise 0/1/2 = なし/ふつう -15 dB/強め -24 dB。
  録り終えて解析する前にかけ、保存する声にも入る。静かな所と声の差が 12 dB 未満なら何もしない）
  v1.3.0: Logic は自動で開かない（autoOpen 標準 OFF、設定 v13 で一度だけ OFF に。黄色いボタンは「MIDI を Finder で表示」、通知クリックは画面を出す）。
  テンポを 2 つに: MidiOptions.bpm = 鼻歌のテンポ（秒→拍）、outBpm = 曲のテンポ（MIDI に書く。0 = 同じ、tempoOut()）。TEMPO の数字は juce::Label で打てる、info.json の "outBpm"
  v1.3.1: アイコンから開くと画面を出す（mac::onReopen が NSApp の delegate に applicationShouldHandleReopen を class_addMethod で足す。JUCE は受けない）。
  MIDI のコピーをダウンロードにも置く（設定 copyTo 1=ダウンロード / 2=デスクトップ / 0=置かない、info.json の "copy"。名前が変わったら前のコピーは消す。
  動かした・消したコピーは読み直しで作り直さない）。通知クリックとメニュー「できた MIDI を Finder で表示」でそのコピーを Finder に

- Sound Sketch（SoundSketch）… 口マネで手元のサンプルを探す単体 Mac アプリ。SketchEngine.h（JUCE なし）で 1 音 48 個の特徴（音量の形・重心の動き・平らさ・帯域・長さ・立ち上がり・重心の傾き）、
  声の重心は voiceToLibrary（log2 Hz を 1.46 倍 − 5.37）で広げてから比べる。Library.h が裏でフォルダを読み ~/Library/Application Support/Sound Sketch/index.bin に保存。
  最初は ~/Splice/sounds（ダウンロード済みだけ。Splice の全カタログは API がないので探さない）。クリックで試聴・ドラッグで Logic・ダブルクリックで「これに似た音」。tools/test_engine.cpp
- Resume Card（ResumeCard）… 中断カード。メニューバー常駐。使っている間 90 秒ごとに状態（前のアプリ・窓・書類・カーソルの前の文・各ブラウザの前のタブ・Finder・前の窓の写真〔ScreenCaptureKit、macOS 14〜〕）を取り、
  画面ロック・スリープ・スクリーンセーバー・操作なし（5 分）でカードに（~/Library/Application Support/Resume Card/cards、60 枚）。3 分以上離れて戻ったら「おかえりなさい」。⌃⌥P 中断＋メモ、⌃⌥R 最後のカード。
  全部開き直す（もう開いているタブへは移るだけ）。パスワード管理アプリは取らない
- Aside（Aside）… ひとりごと秘書。SFSpeechRecognizer（ja-JP、requiresOnDeviceRecognition。できない Mac では聞かない）で約 0.9 秒の無音ごとに確定、
  TaskParser.h（JUCE なし）がきっかけの言葉（あとで・〜しなきゃ・忘れないように・メモして等）のある文だけ「やること」と日時に。EventKit でリマインダーのリスト「ひとりごと」へ、
  打ち合わせ等で時刻があればカレンダー。右上の HUD で取り消し。tools/test_parser.cpp（17 例）
  v1.1.0: 標準は「押したときだけ聞く」（設定 always=false）。Num Deck の「一言メモ」か ⌃⌥M でマイクを開き、一言確定（またはしゃべらず 12 秒）で閉じる。
  分散通知 com.myplugins.aside.command（"memo" / "undo" / "show"）。「いつも聞く」ON のときだけ常に聞く（Logic が前なら休む）
- Third Time（ThirdTime）… 【使っていない: 2026-11-06 本人が「うっとうしい」とのことで Mac から外した（終了してゴミ箱へ）。入れ直さない】「3回目」検出係。CGEventTap（聞くだけ）でアプリ切り替え・⌘/⌃ ショートカット・特別なキー・クリックした所の AX 名を記録（打った文字の中身は記録しない）。
  PatternFinder.h（JUCE なし）が新しい操作で終わる 3〜10 個の並びの 3 回目を探す（前の回の次がどれも同じなら続きを待つ、手が 5 秒止まったら確定、知らせた並びは skipKey＝2 回つなげて回しも除外）。
  「自動にする」で ⌃⌥1〜9、くり返しは AX で同じ名前を押す（なければ窓の中の同じ位置）、文字の入力は止まって待ち ⌃⌥T で続ける。入力監視とアクセシビリティの許可が要る。tools/test_finder.cpp

- Forge Strip（ForgeStrip）… 11 モジュールのチャンネルストリップ。この Mac の定番ストリップ（AU 単体モジュール版）を AVAudioUnit で直接動かして測り、振る舞いを合わせたオリジナル（Source/dsp、JUCE なし、tools/fs_cli.cpp・test_engine.cpp）。
  並び替えできるチェーン（apvts.state の "order"）＋最後にリミッター。SCULPTOR（23 楽器の目標カーブ ToneTargets.h、パワーを保って寄せる、STFT 1024）/ EQ 12 帯域 13 形 / COMP×2（検出 = 3.5 ms の最大、直線の倍率で τ = ATT/2・REL/2、戻りは 1/倍率で τ = min(ATT,REL)、ニー幅 = KNEE×1.5）/
  EXCITER（XY の 4 角の歪み、4 倍 FIR OS）/ TRANSIENT（3 ms 先読み、tsTune）/ GATE（最低 20 ms 開く）/ DENSITY（34.6 ms 先読み）/ CLIPPER / PHASE（IIR の 90° 対）/ UNMASK（サイドチェーン、帯域の効きやすさは測定の表）。各モジュール最大 3 帯域（LR4）
  v1.1.0: コンプ MODE（cmode = Modern / Vintage〔VintageTable.h の 2 次元表、vintTune の 2 段の動き〕/ Punch〔PunchCore、しきい値なし、山にいる間 α(E−B)、下がり始めたら α×16.5/SUSTAIN dB/ms〕）、
  自動ゲイン（2 段、最大 20 dB）、自動リリース（長く下げると最大 2 倍）、EQ の Soft Sat（x − 0.24x|x| + 0.036x²）とダイナミック（しきい値→0 dBFS の直線）、
  SCULPTOR の Add Punch / Add Polish（PunchPolish）、PHASE は直線位相 FIR（遅れ 2401）、LP/HP はマッチド設計。Plugin Link（独自の種類 200、ForgeLink.h）で Forge Mixer から OUTPUT/PAN/WIDTH を動かす
- Forge Mixer（ForgeMixer）… Forge Strip 用のミキサー（音は素通し）。同じ LINK の Strip を点で並べ（左右 = パン、上下 = 音量、大きさ = 幅）、ドラッグの指示を種類 201 で出す（最大 5 つ、放して 0.6 秒）。
  Strip は 30 Hz のタイマーで自分の token の指示をつまみに入れる。ForgeStrip/Source/ForgeLink.h を読む

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
