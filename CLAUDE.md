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
- `MyPlugins/Shared/` … プラグイン共通の部品（Plugin Link、PresetBar＝プリセット保存・取り消し、GainMatch＝GAIN MATCH）。CMakeLists に `../Shared` を足したものは、Shared を変えると一緒にビルドされる（Shared を変えると 20 個以上が全部ビルドになるので、まとめて変える）
- 既存プラグインを変えていないときは再ビルド不要（無駄なビルドは走らせない／走ったら止めてよい）
- Claude 側のブラウザは GitHub にログインしていない。ログインが必要な作業は、本人のいつもの Safari / Chrome で開くよう案内する

## 技術の決まり
- JUCE 8.0.15（CMake の FetchContent で自動取得）、C++17
- CMakeLists は既存プラグイン（`MyPlugins/WoolFuzz` など）と同じ形にする
  - `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0、`COPY_AFTER_BUILD` オプション、`juce_generate_juce_header` を使う
  - `COMPANY_NAME "MyPlugins"`、`PLUGIN_MANUFACTURER_CODE Mypl`
  - `PLUGIN_CODE` は4文字でプラグインごとに別にする。使用済み: `Wlfz`, `Nbrv`（廃止）, `Pchd`, `Blsc`, `Rfrc`（廃止）, `Hzsp`, `Vltm`（廃止）, `Fdpl`, `Gnst`, `Hshf`, `Lmrm`, `Htsc`（廃止）, `Flps`（廃止）, `Smsc`, `Mldv`, `Mdhs`, `Frgs`, `Frgm`（廃止）, `Eldv`, `Ecdr`, `Frbx`, `Aiog`, `Vxch`, `Dcst`, `Drtr`, `Mytr`, `Myfl`, `Mycr`, `Nzot`, `Twtk`, `Rtrt`
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
  - GAIN MATCH（2026-11-09 本人の指示「ゲインマッチがないのはゴミ」）… `MyPlugins/Shared/GainMatch.h`。ON なら入れても・つまみをいじっても音量が変わらない、OFF なら自由。既定 OFF。
    レイアウトに `myp::GainMatch::makeParameter()`（ID `gainMatch`、版のヒント 10＝Logic のパラメーター番号をずらさないため）、
    プロセッサーに `myp::GainMatch gainMatch { *this, myPresets };`（apvts・myPresets より後）、
    prepareToPlay で `prepare`、processBlock の最初に `measureInput`・最後に `process (buffer, ch, 遅れ, amount)`、状態の保存・読み込みで `saveTo` / `loadFrom`、
    内蔵プリセットを読む関数の最後で `gainMatch.requestRelearn()`、updateTrackProperties で `gainMatch.setTrackName`、エディターで `presetBar.showGainMatch (myp::GainMatch::paramId, …)`（ボタンはプリセット欄の左端）。
    測り方は「ON にした時・プラグインの画面でつまみを触った時・プリセットや UNDO を使った時・別のトラックへコピーした時（トラック名で判断）だけ、入口が鳴っている所（と止まってから 3 秒の残り）を K 特性で測る。
    3 秒でほぼ決まり、10 秒たまったら固定」（追いかけ続けない＝コンプにならない。オートメーションの再生・Logic 側の操作では測り直さない。開き直しても固定した値のまま）。上げるのは +12 dB まで。
    amount: MIX 90〜100%（センド使い）は少しずつ 0 へ、DELTA・ソロ・BYPASS など比べる元がない間は 0。最後にリミッターがあるなら ON の間 `limitBoost (0)`。
    内蔵プリセットで全部を初期値に戻す処理からは `gainMatch` を外す。数値テストは build/gmtest/main.cpp（コミットしない）。
    付けないもの: 音を変えない測定器（Balance Scope）、音量を変えるのが仕事のもの（Gain Stage・Fader Pilot）、音源（Sample Sculptor・AIO Guitar・Vox Chop）。
    前からある Horizon Spread `autoGain`・Fire Box `unity`・Mod Host `autoGain` は ID はそのまま、表示を GAIN MATCH にして同じ場所に置いた（2026-11-12: HS・Fire Box も ON の間は OUTPUT / IN で音量を変えないようにそろえた）
  - 画面のいちばん下に「LINK 欄＋GAIN MATCH・プリセット・取り消し欄」（1 段 40 px か 2 段 72 px。GAIN MATCH が入ってプリセット欄が約 550 px になったので、LINK 欄に追加のつまみがあって幅 1200 未満なら 2 段）

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
- 揺らす: トレモロ・オートパン・刻みは My Tremolo（中のプラグインのつまみを揺らすのは Mod Host）。フィルターを動かす（オートワウ・スイープ・S&H）は My Filter
- 古っぽくする（Lo-Fi）: ノイズ・揺れ・歪み・ビット落とし・響き・劣化・エコーをまとめて = Retro Retro（歪みだけは Deca Satu、揺らすだけは My Tremolo）。Lo-Fi の新機能は Retro Retro に足す
- エコー: 普通のエコー（テープ・BBD など）= Echo Drift / 逆再生・音程が変わるエコー・シマー = My Crystal
- 声: Vox Chop = 声を別の声に変える・声を切って鍵盤で弾く（ボーカルチョップ） / Sample Sculptor = 1 つの音を整える。声の加工の新機能は Vox Chop に足す
- 鼻歌: Idea Catcher だけ（録ってから解析 → きれいな MIDI → Logic へドラッグ）。口ドラムは本人が「いらない」ので作らない
- リミッター: 普通のリミッター（2 ミックス・マスター）は本人の Pro-L2 / ドラムは Drum Transient / Forge Strip・Fire Box の最後のリミッターはそれぞれの中だけ
- 抑える: 雑音（サーッ・ファン）・ハム・部屋鳴りを取る = Noise Out / キツい響き・サ行 = Hush Field（Noise Out に De-ess は入れない）
- ダブル・広げる: 1 本のまま広げる（モノにすると元に戻る）= Horizon Spread / 1 本録りの歌・ギターを 2 本録りに（音ごとにずれる別テイク。モノでも厚みが残る）= Twin Take /
  いつも同じずれの加工っぽい厚み = Echo Drift の Chorus Doubler・My Crystal の Detune Double。ダブルの新機能は Twin Take に足す（Horizon Spread の DOUBLE・SHIFT はこれ以上ダブル寄りにしない。歌・ギター用プリセットは本人が使っているので残す、2026-11-12）
- 参照曲まわりの新機能は Reference Monitor にだけ足す（Balance Scope の REF と二重にしない）。Mod Host は「中のプラグインのつまみを動かす」役。帯域・歪み・自動ゲインはこれ以上足さない（Forge Strip の仕事）

### プラグイン（AU）
- Wool Fuzz（WoolFuzz）1.1.0 … ビッグマフ系ファズ。SUSTAIN / TONE / VOLUME。Gain Stage から LEVEL MATCH を受ける
- Porch Drive（PorchDrive）1.1.0 … BD-2系ブルースドライブ。LEVEL / TONE / GAIN。Gain Stage から LEVEL MATCH を受ける
- Lumen Rooms（LumenRooms）1.5.1 … 映画・ポップス向けの広くなめらかな部屋のリバーブ（初期反射＋リバーブ、ECHO、EQ、テンポ同期、FREEZE、自前のプリセット画面）。Fader Pilot から VOCAL DUCK
- Elder Verb（ElderVerb）1.1.1 … 70〜80 年代のデジタルリバーブ機材ふう。22 モード、COLOR 70s/80s/NOW、本物の .vpreset を読める。Fader Pilot から VOCAL DUCK
- Meld Verb（MeldVerb）1.2.1 … EARLY / PLATE / HALL を三角形の BLEND パッドで混ぜる。SMOOTHING・LEARN・UNMASK、SHAPE タブ。Fader Pilot から VOCAL DUCK
- Balance Scope（BalanceScope）1.3.1 … 帯域バランス解析。ジャンル別目標カーブ、REF A/B/C、ラウドネス。出すぎの帯域とオケの音量を送り、Hush Field の削り量・Gain Stage の GAIN を表示
- Horizon Spread（HorizonSpread）1.4.1 … ステレオ感の全部入り（Side だけ加工）。SHAPE / CREATE ページ、GAIN MATCH（ID autoGain）、内蔵プリセット。Fader Pilot が歌っている間 SPREAD を下げる
- Hush Field（HushField）1.2.2 … 動的レゾナンス抑制。帯域カーブ 8 本、STEREO L/R・M/S、SC、LINEAR PHASE / LOW LATENCY。Balance Scope の出すぎ帯域を受け、削り量を送る
- Fader Pilot（FaderPilot）1.2.0 … ボーカルのフェーダーを自動で上げ下げ（遅延ゼロ）。AUTO RIDE / WRITE / FOLLOW MUSIC。フェーダー位置と「歌っているか」を送る
- Gain Stage（GainStage）1.1.0 … トラックの入口の音量をワンクリックでそろえる（遅延ゼロ）。LEARN（鳴っている所だけ 5 秒測り GAIN を 1 回入れる）、TARGET / RMS・PEAK / Ø / MONO / SWAP、IN/OUT メーター。結果は apvts.state の "learnInfo"。GAIN / TARGET などを送る
- Sample Sculptor（SampleSculptor）1.2.0 … サンプル編集（AU 音源 aumu Smsc）。切り取り→PITCH→STRETCH→形→REVERSE→ROOM→DRIVE→TONE→FADE→NORMALIZE、A/B、ドラッグで Logic へ。LINK 欄だけ
- Echo Drift（EchoDrift）1.3.1 … テープ・BBD・ラジオ・コーラス系 31 スタイルのディレイ（市販の定番ディレイを測って合わせ込み）。SINGLE / DUAL / PING-PONG / RHYTHM。Fader Pilot から VOCAL DUCK
- Fire Box（FireBox）1.2.1 … 太く・大きくする全部入り（本人の持っている市販プラグインを測って、音の処理を同じ動きに合わせ込み）。IN / BASS / THICK / BODY / COMP⇄OTT / SOFT CLIP / CRUNCH / MIX / LOUD / OUT、GAIN MATCH（ID unity、4 秒平均で追う。1.2.1 から IN・OUT の分も戻す）・LIVE・192k・BYPASS。測定の道具は build/fuellab（コミットしない）。Plugin Link は Gain Stage で MATCH
  v1.1.0: OTT 作り直し（88 Hz の分割は Q の違う 2 段ずつ、dB 型検出器を持ち上げ用・下げ用に、OTT 時の MIX のドライも帯域分けを通す、量の表 29 段階）。FUEL との差 量24 dB で −33 dB
- Mod Host（ModHost）1.2.0 … 好きな AU を中に読み込み、MOD 1〜4（LFO / ENV / SIDECHAIN / RANDOM / STEPS / LINK）でつまみを動かす。3 帯域・DRIVE・MORPH・前後の解析
- AIO Guitar（AIOGuitar）0.6.0 … 本人のエレキをライン録りした音で鳴らす全部入りギター音源（AU 音源 aumu Aiog ＋単体アプリ、CMakeLists の ALSO_APP で AIOGuitar-App.zip も）。
  MY GUITAR（カポで録った数十音 → 弦・フレットを自動判定、音程・音量を自動補正、録音のビビり（途中で弦がフレットに当たる音）を見つけて増えた分だけ下げる Fix Buzz（0.5.0、BuzzFix.h）、ダウン/アップ交互、1 弦 1 音、強さ SOFT / MEDIUM / HARD の 3 層、STRUM で和音を弦の順にずらす、単体アプリは RECORD GUIDE で 1 音ずつ案内しながら録る）/ SCORE（タブ譜で 1 音ずつ奏法、FOLLOW LOGIC と PLAY IN SYNC）/ EFFECTS / AMP の 4 ページ。
  2026-11-08 本人の判断で全部入り。EFFECTS は Cotton Comp（AIO Guitar だけ）・DRIVE A / B（0.6.0、どちらも 5 機種から選ぶ: Porch Drive・Wool Fuzz（単体と同じ DSP を WoolFuzzDSP.h・PorchDriveDSP.h で共有。変えたら AIO Guitar もビルド）と AIO だけの Hearth Drive（TS 系）・Brass Boost（トレブルブースター）・Barn Dist（RAT 系）、回路をそのまま式に、DrivePedals.h・DriveSlot.h）と AMP・TREM（0.4.0、My Tremolo の小型版。MyTremoloDSP.h を共有。My Tremolo の Source を変えたら AIO Guitar もビルド）を好きな順に並べる（ドラッグ / ◀ ▶、CAB はいつも最後、Rig.h）。ディレイ・リバーブは入れない（内蔵リバーブも 0.3.0 で外した。自作を後ろに）。
  AMP は 5 種類＋キャビ 5 種類（か IR）＋マイク 3 種類・位置・距離。Logic の Amp Designer を 57 通りバウンスして合わせ込み（build/AmpLab、AmpTables.h は自動生成）。Plugin Link は LINK 欄だけ（Kind 17 は未登録）
  2026-11-11 本人: AIO に入れるのは AMP の前の足元のペダルだけ（2 回目ワウ、3 回目フェイザー／ヴァイブ、箱は 7 個・歪みは 6〜7 機種まで）、アンプは今の 5 種類を残して足す（Guitar Rig は無料版だけなので見送り）。DOUBLE は AIO に作らず、AIO の後ろに Twin Take（2026-11-12 本人）。
  この Mac の Guitar Rig 7 は無料版（Player）で、Pro の部品は測れない（NOTES.md の DRIVE A / B）
- Forge Strip（ForgeStrip）1.3.1 … 11 モジュールのチャンネルストリップ（並び替え可、最後にリミッター）。Plugin Link の相手だった Forge Mixer は削除（受け取る処理と独自の種類 200 / 201 はソースに残っている）
- Vox Chop（VoxChop）1.0.0 … 自分の声を別の声に変えて、切って鍵盤で弾く AU 音源（aumu Vxch）。CAPTURE（サイドチェーン）かファイル → 後から丸ごと計算（TD-PSOLA、VoxEngine.h）。
  VOICE MAP（高さ × 体の大きさ）・BREATH・GRIT・ROBOT・ALIEN・声のプリセット 23 個 / AUTO（言葉の切れ目）・GRID で最大 64 片を C1 から / 片ごとの設定・MELODIC・WHOLE / 片・全部・MIDI・全体をドラッグで Logic へ。
  空間系・歪み・ボコーダー・ケロケロは入れない（自作と Logic 標準を後ろに）。Plugin Link は LINK 欄だけ（Kind 18 は未登録）
- Drum Transient（DrumTransient）1.1.0 … ドラム専用のトランジェント・リミッター（Uni-L を測って合わせ込み、ドラム用に絞った）。PUSH / PUNCH / BODY / CLIP⇄LIMIT / RELEASE / LOW KEEP / CEILING、LINK・DELTA、
  LEARN（PUSH を自動で 1 回）、ATTACK と BODY を流す表示、内蔵プリセット 6 個。キック・スネアは Fire Box と 2 つ挿し（自分のプリセット「キック迫力」など＋チャンネルストリップ設定）。遅れ 9.0 ms。GAIN MATCH は下げるだけ（limitBoost 0）。Plugin Link は LINK 欄だけ（Kind 19 は未登録）

- Deca Satu（DecaSatu）1.1.0 … トラック用サチュレーター（本人の Mac の市販サチュレーターを測って合わせ込み）。STYLE A/E/N/T/P・DRIVE・PUNISH・LOW CUT＋THUMP・TONE・HIGH CUT＋STEEP・MIX・OUTPUT。
  本物の AUTO は GAIN MATCH に統合（ON の間だけ AUTO の量を中で使う）。DSP は DecaSatuDSP.h（JUCE なし、後で Forge Strip に入れられる）。Plugin Link は Gain Stage の LEVEL MATCH だけ（Kind 20 は未登録）。測定の道具は build/decalab

- My Tremolo（MyTremolo）1.1.1 … トレモロの全部入り（本人の Mac の市販トレモロを測って合わせ込み）。DEPTH / GROOVE / FEEL / ACCENT / RATE＋SYNC＋TAP / WIDTH / LOW KEEP、
  RHYTHM（4 Bars〜1/32、付点・3 連、Pattern = 升目で描く）、SHAPE 15 種＋Custom（点で描く、SMOOTH 5 種）、ENVELOPE（THRESHOLD・ATTACK・RELEASE・RATE MOD ±6 oct・DEPTH MOD・Env/Gate）、
  STYLE 8 種（Digital＋測った色付け 7 種）、内蔵プリセット 30 個。DSP は MyTremoloDSP.h（JUCE なし）。Plugin Link は Fader Pilot の歌で揺れを浅く（Kind 21 は未登録）。測定の道具は build/tremlab

- My Filter（MyFilter）1.1.0 … 音に合わせて動くフィルターの全部入り（本人の Mac の市販フィルター 1 つ版・2 つ版を測って合わせ込み、1 つにまとめた）。
  LP / BP / HP / Notch・2〜8 極・RES（0.965 から上は自己発振）、1 Filter / 2 Filters（Serial / Parallel・LINK）、MOD × FREQ MOD（オクターブ）、
  動かし方 LFO / RHYTHM / ENVELOPE / RANDOM / STEP / ADSR、TWEAK（RES MOD・LEVEL MOD・L/R OFFSET / MODE はモノ → ステレオだけ）、色付け 7 種類＋DIGITAL、内蔵プリセット 16 個。
  揺れの時計・形・パターン・色付けは My Tremolo の Source を include（My Tremolo の Source を変えたら My Filter もビルド）。Plugin Link は Fader Pilot の歌で MOD を小さく（Kind 22 は未登録）。測定の道具は build/fflab

- My Crystal（MyCrystal）1.0.0 … 音を短く切って音程を変え・逆再生しながら重ねるエコー（本人の Mac の市販グラニュラー・エコーを測って合わせ込み）。
  PITCH ±3 oct（半音で止まる、shift でセント）/ SPLICE / DELAY（SYNC で音符）/ RECYCLE / REVERSE / ANALOG、GATE⇄DUCK、TWEAK（PITCH・SPLICE・DELAY OFFSET、SMOOTHING、LOW / HIGH CUT は輪の中、ATTACK / RELEASE、DUCK MODE、Mixed / Dual / Ping-Pong）、内蔵プリセット 15 個。
  センドで使う飛び道具。遅れなし。色付け・4 倍は My Tremolo の Source を include（変えたら My Crystal もビルド）。Plugin Link は Fader Pilot の歌でウェットを下げる（Kind 23 は未登録）。測定の道具は build/crylab

- Noise Out（NoiseOut）1.0.0 … 録った音の雑音・ハム・部屋鳴りを取る（本人の Mac の iZotope RX 12 試用版を約 830 通り測って合わせ込み）。並び固定 HUM → NOISE → ROOM。
  HUM = RX 12 De-hum の Static と同じ形（RBJ ピーキング Q 1000 を倍音に並べる＋1.5 Hz の直流カット。波形の差 −80 dB）、AUTO は 50 / 60 Hz ±0.3 Hz を自分で探す（楽器の音程を拾わない）/
  NOISE = Voice De-noise（Dialogue / Surgical）に合わせた STFT のノイズ除去、ADAPTIVE / LEARNED（LEARN は 3 秒で自動終了）/ ROOM = De-reverb に寄せた響きの予想と引き算。
  遅れ 2048（48 kHz）。DELTA。Plugin Link は Fader Pilot が歌っている間 ADAPTIVE の推定を止める（Kind 24 は未登録）。測定の道具は build/noiselab

- Twin Take（TwinTake）1.1.0 … 1 本録りの歌・ギターを「2 回録って重ねた」ように聞かせるダブラー（2026-11-11。提案の Double Take は実在製品名なので改名）。
  音の頭ごとに各ダブルの遅れ・音程（±2〜9 セント）・音色・音量を選び直す。VOCAL（遅れ 0、ダブル 1/2/4 本、子音・息はダブルしない）/ GUITAR（原音を左・ダブルを右、40 ms の遅れを申告）。
  1.1.0: 語尾（原音が先に終わったらダブルも下げる）・長い音の中の小さな揺れ（VOCAL だけ）・DOUBLE ONLY（原音を消してダブルだけ、AUX のセンド用。プリセット・UNDO の対象外）。
  DSP は TwinTakeDSP.h（JUCE なし）、試作・テストは tools/render.cpp。Plugin Link は LINK 欄だけ（Kind 25 は未登録）

- Retro Retro（RetroRetro）1.0.0 … 音を古っぽくする全部入り（本人の Mac の市販 Lo-Fi プラグインを測って合わせ込み。2026-11-12 本人「rc version2」→ 実在製品名に近いので「Retro Retro」に）。
  NOISE（計算で作る 10 種類）/ WOBBLE / DISTORT 9 種類 / DIGITAL / SPACE 10 種類（FDN）/ LOSS（MAGNETIC・CODEC）/ ECHO ＋ MASTER（VARISPEED・EQ・COMPRESS・WIDTH）、MAGNITUDE で全部の量に掛け算。
  表（MAIN）はモジュールごとに大きいつまみ 1 本＋種類、全部のつまみは DETAIL。相手の録音データ（ノイズ素材・IR・プリセット）は使っていない。
  4 倍は My Tremolo の Source を include（変えたら Retro Retro もビルド）。遅れ 283（48 kHz）。Plugin Link は Fader Pilot の歌でノイズを下げる（Kind 26 は未登録）。測定の道具は build/rclab

### 単体 Mac アプリ（.app）
- Reference Monitor（ReferenceMonitor）1.6.0 … 他人の曲との聴き比べ（macOS 14.2〜、プロセスタップで Logic とミュージックの音を受ける）。TONE / WIDTH / PUNCH / LOW 表示、SNAP、AVG、LISTEN AS。分散通知で Num Deck から操作
- Mix Time Machine（MixTimeMachine）1.0.0 … バウンス履歴と聴き比べ。~/Music/Logic（と Bounces フォルダ）を見張って新しいバウンスを自動で取り込み、
  FLAC でコピー保存（~/Music/Mix Time Machine）。A/B を音量合わせ・頭のずれ自動合わせで切替、5回のブラインドテスト、何が変わったか（音量・PLR・広がり・音色差）
- Num Deck（NumDeck）1.25.0 … テンキーに Logic などの操作を割り当てる（3×3 タイル画面、Logic Pro / 業務モード、マクロ、2 画面）。
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

## 夜の健康診断（2026-11-11 本人の GO）
- `tools/health-check/`（hc.cpp = 自前のオフラインのホスト、nightly.sh、NIGHTLY_PROMPT.md、KNOWN.md、README.md）。入っている Mypl の AU を全部、検査ごとに別プロセスで読み込む
  （NaN・ピーク・既定の音量差・DC・CPU・遅れの申告・鳴り止むか・リセット・プチノイズ・ランダム設定・保存・ブロックの大きさ・サンプルレート・モノ・GAIN MATCH・auval・入っている版）
- 測るのは Mac の自動実行（launchd、毎晩 3 時に `nightly.sh --nightly`。Claude の利用枠を使わない）。登録は本人が `bash tools/health-check/install-nightly.sh`（Claude からの登録は安全確認で止められる）。
  Logic が開いていても止まっていれば測る（再生・録音中＝Logic の CPU 20% 以上の間だけ待つ、最大 4 時間）。様子は build/health/nightly-status.txt
- Claude アプリの定期実行「【毎晩】自作プラグイン健康診断」（taskId plugin-health-check、毎朝 7:30）が結果を読み、引っかかった所のコードを読んで報告する（build/health/reports/）。3 時に測れていなければ自分で測る。コードは直さない
  （2026-11-12: 前の作りは 3 時の Claude が利用枠の上限で動けず、Logic が開いていると朝まで待っていた）
- 結果は build/health/runs/<日時>/summary.md（最新は build/health/latest、毎晩の最新は build/health/nightly）。手で: `bash tools/health-check/nightly.sh --only <コード>`（手で動かすときは Logic を待たない）
- 「健康診断の 1 を直して」と言われたら、いちばん新しい報告のその番号を、いつもの決まり（DSP 検査係 → push → ビルド → インストール）で直す
- 仕様と決めたものは KNOWN.md へ（本人が決める）。新しいプラグインを足しても検査は自動で増える（hc.cpp を変える必要はない）

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
