# プラグイン作りプロジェクト（Logic Pro 用）

このファイルは Claude Code が自動で読む「前提メモ」です。claude.ai のプロジェクト「プラグイン作り」の指示をここに移したものです。

## いちばん大事な決まり
- **説明・返事・報告はすべて日本語で書く**（2026-11-06 本人の指示。すべてのチャット・セッションで守る）。
  係（サブエージェント）の結果を伝えるときも日本語。コード・コマンド・ボタン名はそのまま書いてよい

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
    - push 時は、変更があったフォルダのものだけビルドされる（build.yml や CLAUDE.md、README.md・NOTES.md など .md だけの変更では何もビルドしない）
    - 手動で動かすとき（Actions →「Run workflow」）は、`only` に名前を入れるとそれだけ、空なら全部ビルド
      - Claude から: `gh api -X POST repos/toratora2815-ai/plugins/actions/workflows/build.yml/dispatches -f ref=main -f "inputs[only]=<名前>"`
      - GitHub の Mac（arm64）が混んでビルドが始まらないときは、`-f "inputs[runner]=macos-15-intel"` を付けると別の Mac で動く
    - ビルド結果は Artifacts と、Releases の `latest` に置かれる
  - `MyPlugins/<名前>/` … 各プラグイン（CMakeLists.txt, Source/, README.md）
- 新しいプラグインを足しても build.yml を書き換える必要はない（自動で見つかる）
- `MyPlugins/Shared/` … プラグイン共通の部品（Plugin Link、PresetBar＝プリセット保存・取り消し）。CMakeLists に `../Shared` を足したものは、Shared を変えると一緒にビルドされる（Shared を変えると 14 個ほど全部ビルドになるので、まとめて変える）
- 既存プラグインを変えていないときは再ビルド不要（無駄なビルドは走らせない／走ったら止めてよい）
- Claude 側のブラウザは GitHub にログインしていない。ログインが必要な作業は、本人のいつもの Safari / Chrome で開くよう案内する

## 技術の決まり
- JUCE 8.0.15（CMake の FetchContent で自動取得）、C++17
- CMakeLists は既存プラグイン（`MyPlugins/WoolFuzz` など）と同じ形にする
  - `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0、`COPY_AFTER_BUILD` オプション、`juce_generate_juce_header` を使う
  - `COMPANY_NAME "MyPlugins"`、`PLUGIN_MANUFACTURER_CODE Mypl`
  - `PLUGIN_CODE` は4文字でプラグインごとに別にする。使用済み: `Wlfz`, `Nbrv`（廃止）, `Pchd`, `Blsc`, `Rfrc`（廃止）, `Hzsp`, `Vltm`（廃止）, `Fdpl`, `Gnst`, `Hshf`, `Lmrm`, `Htsc`（廃止）, `Flps`（廃止）, `Smsc`, `Mldv`, `Mdhs`, `Frgs`, `Frgm`（廃止）, `Eldv`, `Ecdr`, `Frbx`
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
細かい仕組み・ID・版ごとの変更は各フォルダの `NOTES.md`（Claude 用の作業メモ）、本人向けの説明は `README.md`。
版を上げたら NOTES.md に 1 行足し、ここは「何用か・最新版・つながる相手」が変わったときだけ直す。一覧は `MyPlugins/README.md` にも。

### 迷ったときの早見
- リバーブ: Lumen Rooms = 細かく作り込む部屋・ホール、長い響き / Elder Verb = 昔のデジタル機材の色付き / Meld Verb = ボーカルやミックスで自動で引っ込む実用リバーブ
- 聴き比べ: Reference Monitor = 他人の曲と / Mix Time Machine = 自分の前のバウンスと（マスタリング段階で使う） / Balance Scope = トラックに挿す測定器・Plugin Link の配り役
- 鼻歌: Idea Catcher だけ（録ってから解析 → きれいな MIDI → Logic へドラッグ）。口ドラムは本人が「いらない」ので作らない
- 参照曲まわりの新機能は Reference Monitor にだけ足す（Balance Scope の REF と二重にしない）。Mod Host は「中のプラグインのつまみを動かす」役。帯域・歪み・自動ゲインはこれ以上足さない（Forge Strip の仕事）

### プラグイン（AU）
- Wool Fuzz（WoolFuzz）1.0.0 … ビッグマフ系ファズ。SUSTAIN / TONE / VOLUME。Gain Stage から LEVEL MATCH を受ける
- Porch Drive（PorchDrive）1.0.0 … BD-2系ブルースドライブ。LEVEL / TONE / GAIN。Gain Stage から LEVEL MATCH を受ける
- Lumen Rooms（LumenRooms）1.4.0 … 映画・ポップス向けの広くなめらかな部屋のリバーブ（初期反射＋リバーブ、ECHO、EQ、テンポ同期、FREEZE、自前のプリセット画面）。Fader Pilot から VOCAL DUCK
- Elder Verb（ElderVerb）1.0.0 … 70〜80 年代のデジタルリバーブ機材ふう。22 モード、COLOR 70s/80s/NOW、本物の .vpreset を読める。Fader Pilot から VOCAL DUCK
- Meld Verb（MeldVerb）1.1.0 … EARLY / PLATE / HALL を三角形の BLEND パッドで混ぜる。SMOOTHING・LEARN・UNMASK、SHAPE タブ。Fader Pilot から VOCAL DUCK
- Balance Scope（BalanceScope）1.3.1 … 帯域バランス解析。ジャンル別目標カーブ、REF A/B/C、ラウドネス。出すぎの帯域とオケの音量を送り、Hush Field の削り量・Gain Stage の GAIN を表示
- Horizon Spread（HorizonSpread）1.3.1 … ステレオ感の全部入り（Side だけ加工）。SHAPE / CREATE ページ、AUTO GAIN、内蔵プリセット。Fader Pilot が歌っている間 SPREAD を下げる
- Hush Field（HushField）1.1.0 … 動的レゾナンス抑制。帯域カーブ 8 本、STEREO L/R・M/S、SC、LINEAR PHASE / LOW LATENCY。Balance Scope の出すぎ帯域を受け、削り量を送る
- Fader Pilot（FaderPilot）1.2.0 … ボーカルのフェーダーを自動で上げ下げ（遅延ゼロ）。AUTO RIDE / WRITE / FOLLOW MUSIC。フェーダー位置と「歌っているか」を送る
- Gain Stage（GainStage）1.1.0 … トラックの入口の音量をワンクリックでそろえる（遅延ゼロ）。LEARN（鳴っている所だけ 5 秒測り GAIN を 1 回入れる）、TARGET / RMS・PEAK / Ø / MONO / SWAP、IN/OUT メーター。結果は apvts.state の "learnInfo"。GAIN / TARGET などを送る
- Sample Sculptor（SampleSculptor）1.2.0 … サンプル編集（AU 音源 aumu Smsc）。切り取り→PITCH→STRETCH→形→REVERSE→ROOM→DRIVE→TONE→FADE→NORMALIZE、A/B、ドラッグで Logic へ。LINK 欄だけ
- Echo Drift（EchoDrift）1.2.0 … テープ・BBD・ラジオ・コーラス系 31 スタイルのディレイ（市販の定番ディレイを測って合わせ込み）。SINGLE / DUAL / PING-PONG / RHYTHM。Fader Pilot から VOCAL DUCK
- Fire Box（FireBox）1.1.0 … 太く・大きくする全部入り（本人の持っている市販プラグインを測って、音の処理を同じ動きに合わせ込み）。IN / BASS / THICK / BODY / COMP⇄OTT / SOFT CLIP / CRUNCH / MIX / LOUD / OUT、UNITY・LIVE・192k・BYPASS。測定の道具は build/fuellab（コミットしない）。Plugin Link は Gain Stage で MATCH
  v1.1.0: OTT 作り直し（88 Hz の分割は Q の違う 2 段ずつ、dB 型検出器を持ち上げ用・下げ用に、OTT 時の MIX のドライも帯域分けを通す、量の表 29 段階）。FUEL との差 量24 dB で −33 dB
- Mod Host（ModHost）1.1.0 … 好きな AU を中に読み込み、MOD 1〜4（LFO / ENV / SIDECHAIN / RANDOM / STEPS / LINK）でつまみを動かす。3 帯域・DRIVE・MORPH・前後の解析
- Forge Strip（ForgeStrip）1.2.1 … 11 モジュールのチャンネルストリップ（並び替え可、最後にリミッター）。Plugin Link の相手だった Forge Mixer は削除（受け取る処理と独自の種類 200 / 201 はソースに残っている）

### 単体 Mac アプリ（.app）
- Reference Monitor（ReferenceMonitor）1.6.0 … 他人の曲との聴き比べ（macOS 14.2〜、プロセスタップで Logic とミュージックの音を受ける）。TONE / WIDTH / PUNCH / LOW 表示、SNAP、AVG、LISTEN AS。分散通知で Num Deck から操作
- Mix Time Machine（MixTimeMachine）1.0.0 … バウンス履歴と聴き比べ。~/Music/Logic（と Bounces フォルダ）を見張って新しいバウンスを自動で取り込み、
  FLAC でコピー保存（~/Music/Mix Time Machine）。A/B を音量合わせ・頭のずれ自動合わせで切替、5回のブラインドテスト、何が変わったか（音量・PLR・広がり・音色差）
- Num Deck（NumDeck）1.24.1 … テンキーに Logic などの操作を割り当てる（3×3 タイル画面、Logic Pro / 業務モード、マクロ、2 画面）。
  仕事のリンク（URL）は公開リポジトリに書かず、本人の設定ファイル（~/Library/Application Support/Num Deck）にだけ入れる。今の本人の設定は Home = ON/OFF、Clear = モード切替
- Idea Catcher（IdeaCatcher）1.4.0 … 思いつきの鼻歌を録って、キー・テンポ入りの MIDI にするメニューバー常駐アプリ。録音キー ⌃⌥I、さかのぼり録音、ノイズ除去、重ね録り。
  録ってから解析（遅れなし）→ 余分な音の掃除 → クオンタイズ（細かさ・強さ）→ 手直しの画面 → ピアノロールをつかんで Logic へドラッグ。Logic は開かない・操作しない。
  口ドラムはなし（2026-11-06 本人「いらない」）。VoiceEngine.h / KeyFinder.h はもう Idea Catcher のもの
- Sound Sketch（SoundSketch）1.0.0 … 口マネで手元のサンプルを探す。SketchEngine.h（JUCE なし）で 1 音 48 個の特徴（音量の形・重心の動き・平らさ・帯域・長さ・立ち上がり・重心の傾き）、
  声の重心は voiceToLibrary（log2 Hz を 1.46 倍 − 5.37）で広げてから比べる。Library.h が裏でフォルダを読み ~/Library/Application Support/Sound Sketch/index.bin に保存。
  最初は ~/Splice/sounds（ダウンロード済みだけ。Splice の全カタログは API がないので探さない）。クリックで試聴・ドラッグで Logic・ダブルクリックで「これに似た音」。tools/test_engine.cpp
- Aside（Aside）1.1.0 … ひとりごと秘書。言ったことから「やること」を拾ってリマインダー／カレンダーへ。標準は押したときだけ聞く（Num Deck の「一言メモ」か ⌃⌥M）

### Plugin Link（MyPlugins/Shared/PluginLink.h・PluginLinkJuce.h）
自作プラグイン同士の連携（共有メモリの掲示板、LINK 1〜4、FROM で相手選び）。詳しい並び・今つながっているもの・空き番号は `MyPlugins/Shared/NOTES.md`。
新しくつなぐときは Kind と送る数値の番号を PluginLink.h に足す。Kind 6・7（削除した Reference・Nebula）は空き番号のまま使い回さない。Forge は独自の 200 / 201（Forge Mixer は削除）

### やめたもの（作り直さない。ソースは git の履歴にだけ残る）
- Velvet Tamer（Vltm）… 2026-11-06。ややこしいので Hush Field に一本化
- Hum Keys … 2026-11-06。その場で MIDI にする遅れ（低い声で 30〜90 ms）が仕組み上縮められず「疲れる」。クオンタイズ・手直しは Idea Catcher に移した
- Hit Sculptor（Htsc）・Flipside（Flps）… 2026-11-06。本人の意図は「サンプル編集」だったので Sample Sculptor にまとめた
- Third Time … 2026-11-06。「うっとうしい」
- Forge Mixer（Frgm）… 2026-11-06。本人「消そっか」
- Nebula Reverb（Nbrv）… 2026-11-06。Lumen Rooms と被るので
- Reference（プラグイン、Rfrc）… 2026-11-06。Reference Monitor（アプリ）で足りる
- Resume Card … 2026-11-06。「うっとうしい」
- 消したものの zip は、build.yml が Releases の latest から自動で消す（MyPlugins/ にフォルダがないもの）

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
- GitHub の Mac が借りられないときは、build/tools/venv の cmake + Ninja + build/_juce でこの Mac でビルドできる（本人が頼んだとき）
- `build/` はコミットしない
