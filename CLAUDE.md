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
- 既存プラグインを変えていないときは再ビルド不要（無駄なビルドは走らせない／走ったら止めてよい）
- Claude 側のブラウザは GitHub にログインしていない。ログインが必要な作業は、本人のいつもの Safari / Chrome で開くよう案内する

## 技術の決まり
- JUCE 8.0.15（CMake の FetchContent で自動取得）、C++17
- CMakeLists は既存プラグイン（`MyPlugins/WoolFuzz` など）と同じ形にする
  - `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0、`COPY_AFTER_BUILD` オプション、`juce_generate_juce_header` を使う
  - `COMPANY_NAME "MyPlugins"`、`PLUGIN_MANUFACTURER_CODE Mypl`
  - `PLUGIN_CODE` は4文字でプラグインごとに別にする。使用済み: `Wlfz`, `Nbrv`, `Pchd`, `Blsc`, `Rfrc`, `Hzsp`, `Vltm`, `Fdpl`
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
- Reference（Reference）… 自分の曲と参照曲の聴き比べプラグイン（ファイル／ライブ入力、音量自動合わせ）
- Horizon Spread（HorizonSpread）… ステレオ感を出す手法の全部入り。Mid は触らず Side だけ加工（モノラル互換）。
  第1段階 SHAPE ページ: WIDTH 3帯域＋AUTO / AIR / SIDE DRIVE / TRANSIENT、SAFETY: BASS MONO / GUARD、MONO CHECK、ベクトルスコープ。
  CREATE ページ（v1.1.0）: SHIFT（SAFE/CLASSIC）/ DOUBLE / BANDS（ALT/TILT）/ HAZE（CLOUD/SMOOTH）/ MOTION。
  順番は「生み出す → 整える」で固定。MIX と GUARD の基準は CREATE 前の入力
  v1.2.0: AUTO GAIN（標準 ON）、SIDE SOLO、内蔵プリセット11個、帯域ごとの広さメーター、MOTION のテンポ同期、
  SHIFT CLASSIC の L/R 選択、DYNAMIC（DYNAMICS タイル）。量のつまみはどのタイルでも右側に置く
- Velvet Tamer（VelvetTamer）… レゾナンス抑制（耳に痛い響きを鳴った瞬間だけ自動で下げる）。STFT で「まわりより飛び出た所」を検出。
  DEPTH / SHARPNESS / SELECTIVITY / ATTACK / RELEASE / FOCUS LOW・HIGH / MIX / OUTPUT / DELTA / LINK。遅延 2048 サンプル（48k）。
  v1.1.0: グラフ上で FOCUS をドラッグ、WEIGHT（7 点固定周波数の効きの強弱カーブ、ID weight0〜6）、プリセットメニュー
  v1.2.0: LOW LATENCY（ID lowlatency、遅延 512 サンプル。エンジンを 2 つ用意して切り替え）、SIDECHAIN（ID sidechain、外部サイドチェーンの音で下げる帯域を決める）
- Fader Pilot（FaderPilot）… ボーカルの音量を自動でフェーダーを上げ下げして揃える（遅延ゼロ）。TARGET / FLOOR / RANGE / SPEED / OUTPUT、
  AUTO RIDE（OFF で Ride パラメーター＝オートメーションをそのまま使う）、WRITE（再生中に Ride へ書き込み、Logic の Latch/Touch で記録）、
  FOLLOW MUSIC（サイドチェーンのオケの音量＋OFFSET を目標に）、プリセット8個
- Reference Monitor（ReferenceMonitor）… 聴き比べ用の単体 Mac アプリ（macOS 14.2 以降、プロセスタップで Logic とミュージックの音を受け取る）。
  v1.1.0: LISTEN AS（PHONE / LAPTOP / EARBUDS / BT SPEAKER / CAR / CLUB の聞こえ方、ピンクノイズで音量を自動補正）、
  LEVEL に SPOTIFY / APPLE / YOUTUBE の音量正規化、メーターに各サービスで何 dB 上げ下げされるか
  v1.2.0: 幅 720〜（MacBook 画面の半分）に収まる縦並びレイアウト。左の参照曲リストは上の REF メニューに
- Mix Time Machine（MixTimeMachine）… バウンス履歴と聴き比べの単体 Mac アプリ。~/Music/Logic（と Bounces フォルダ）を見張って新しいバウンスを自動で取り込み、
  FLAC でコピー保存（~/Music/Mix Time Machine）。A/B を音量合わせ・頭のずれ自動合わせで切替、5回のブラインドテスト、何が変わったか（音量・PLR・広がり・音色差）
- Num Deck（NumDeck）… テンキー 1〜9 に Logic の操作を割り当てる単体 Mac アプリ（ゲーム機メニュー風の 3×3 タイル画面）。
  CGEventTap でテンキーを横取り → Logic の初期キーコマンドを送る（アクセシビリティ許可が必要）。3 ページ（−/+ で切替）、
  Clear（NumLock）で ON/OFF、「Logic が前面のときだけ」（標準 ON）、タイルクリックで割り当て変更・ショートカットを覚えさせる
  v1.1.0: モード（Logic Pro / 業務、クリックでだけ切替）、各 4 ページ。業務はどのアプリでも動く（コピー等・タブ操作・アプリを開く・ページを開く）。
  Chrome の前面タブを AppleScript で登録。仕事のリンク（URL）は公開リポジトリに書かず、本人の設定ファイル（~/Library/Application Support/Num Deck）にだけ入れる
  v1.2.0: 小さい画面（幅 640 / 高さ 440 未満でコンパクト配置、設定から 430×300 に切替）、ウィンドウの位置と大きさを保存
  v1.3.0: Logic Pro モード 4 ページ目「リファレンス」で Reference Monitor を操作（分散通知 com.myplugins.referencemonitor.command、
  object に "ab" / "mix" / "ref" / "play" / "cue1" など。Reference Monitor 1.3.0 の RemoteCommands が受け取る）
  v1.3.1: 1 ページ目の 3（キャプチャ録音）を Reference Monitor の出力 A/B 切替（"out" / "outA" / "outB"）に
  v1.3.2: 1 ページ目の 1（先頭へ）をトラックのフリーズに（⌃⌥⇧⌘F を送る。Logic のキーコマンド「トラックのフリーズを切り替え」に本人が 1 回覚えさせる）
  v1.3.3: Logic Pro モードのキー送信は前面アプリに関係なく Logic へ（KeyTap::sendKeyToLogic。Logic は後ろにいる間のキーを受けないので一瞬前に出して送り、150 ms 後に元のアプリへ戻す）。アプリのアイコン（Resources/Icon.png、tools/make-app-icons.swift で描く。Reference Monitor 1.3.2 も同じ）

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
