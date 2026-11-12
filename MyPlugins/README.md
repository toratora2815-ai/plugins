# MyPlugins の一覧

Mac の Logic Pro で使う、自作のプラグイン（AU）と Mac アプリ（.app）です。
GitHub に push すると自動でビルドされ、Releases の「latest」に置かれます。くわしい使い方は、各フォルダの README を見てください。

## プラグイン（AU。Logic の「Audio Units → MyPlugins」、Sample Sculptor・AIO Guitar・Vox Chop・Hybrid Kit は音源の欄）
| 名前 | フォルダ | 何用か |
|---|---|---|
| Wool Fuzz | WoolFuzz | ビッグマフ系の太いファズ |
| Porch Drive | PorchDrive | ブルース系オーバードライブ（BD-2 系の味） |
| Deca Satu | DecaSatu | トラックごとに挿す色付けの歪み（5 つの性格。軽く太く〜完全につぶすまで） |
| Fire Box | FireBox | 太く・大きくする全部入り（低音・厚み・コンプ / OTT・ソフトクリップ・音圧） |
| Lumen Rooms | LumenRooms | 部屋・ホールを細かく作り込むリバーブ。長い響きも |
| Elder Verb | ElderVerb | 70〜80 年代のデジタルリバーブ機材ふうの、色の付いたリバーブ |
| Meld Verb | MeldVerb | EARLY / PLATE / HALL を三角形で混ぜる、自動で引っ込む実用リバーブ |
| My Tremolo | MyTremolo | トレモロの全部入り（形・リズム描き・スウィング・アクセント・入力で速さ/深さが変わる・7 種類の色付け・オートパン） |
| My Filter | MyFilter | 音に合わせて動くフィルターの全部入り（オートワウ・スイープ・刻み・サンプル＆ホールド・ADSR、フィルター 2 つの直列 / 並列、7 種類の色付け） |
| My Crystal | MyCrystal | 音を短く切って音程を変え・逆再生しながら重ねるエコー（オクターブ上のシマー、逆再生の飛ばし、5 度のパッド。センドで使う） |
| Echo Drift | EchoDrift | テープ・BBD・ラジオなど 31 種類の性格を持つディレイ |
| Retro Retro | RetroRetro | 古いレコード・カセット・壊れかけのテープ・昔のサンプラーの音に（ノイズ・揺れ・歪み・ビット落とし・響き・劣化・エコーを MAGNITUDE 1 本でまとめて） |
| Horizon Spread | HorizonSpread | モノラルで崩れないステレオの広げ役（1 本のまま広げる） |
| Twin Take | TwinTake | 1 本録りの歌・ギターを「2 回録って重ねた」ように聞かせるダブラー（音ごとにずれ方が変わる別テイク感） |
| Hush Field | HushField | 耳に刺さる響きを鳴った瞬間だけ下げる（動的レゾナンス抑制） |
| Noise Out | NoiseOut | 録った音の雑音（サーッ・ファン）・ハム（ブーン）・部屋鳴り・口のピチャ音・息・吹かれを取る（ボーカル・ナレーション・ライン録り用） |
| Drum Transient | DrumTransient | ドラム専用のリミッター。突っ込んでもアタックが死なず胴も揺れない（普通のリミッターは Pro-L2） |
| Forge Strip | ForgeStrip | EQ・コンプなど 11 モジュールのチャンネルストリップ |
| Gain Stage | GainStage | トラックの入口の音量をワンクリックでそろえる |
| Fader Pilot | FaderPilot | ボーカルの音量を自動でフェーダーを動かして揃える |
| Balance Scope | BalanceScope | 帯域バランスとラウドネスの測定器。Plugin Link の配り役 |
| Mod Host | ModHost | 好きな AU を中に入れて、どのつまみでも LFO などで動かす |
| Sample Sculptor | SampleSculptor | サンプルを落として加工し、ドラッグで Logic に戻す（AU 音源） |
| AIO Guitar | AIOGuitar | 自分のエレキを録った音で鳴らす全部入りギター音源（AU 音源＋単体アプリ） |
| Vox Chop | VoxChop | 自分の声を別の声（女性・男性・中性・子ども・ささやき・ロボット…）に変えて、切って鍵盤で弾く（AU 音源） |
| Hybrid Kit | HybridKit | 生ドラム（Logic の Drum Kit Designer の録音）に EDM の音（808 のサブ・クラップ）を部品ごとに重ねるドラム音源。Drummer トラックでそのまま鳴る。キック・スネアなどを別の出口へ（マルチアウト）（AU 音源） |

## アプリ（.app）
| 名前 | フォルダ | 何用か |
|---|---|---|
| Reference Monitor | ReferenceMonitor | 自分のミックスと他人の曲（参照曲）を聴き比べる |
| Mix Time Machine | MixTimeMachine | 自分の前のバウンスと聴き比べる（バウンス履歴） |
| Idea Catcher | IdeaCatcher | 思いつきの鼻歌を録って、キーとテンポ入りの MIDI に。できた MIDI は Logic へドラッグ（メニューバー常駐） |
| Sound Sketch | SoundSketch | 口マネで手元のサンプルを探す |
| Num Deck | NumDeck | テンキーに Logic などの操作を割り当てる |
| Aside | Aside | ひとりごとをリマインダー・カレンダーに入れる秘書 |

- `Shared` … プラグイン共通の部品（Plugin Link、プリセット保存・取り消しの帯、GAIN MATCH）。単体では使いません

## 迷いやすいものの早見
- **リバーブ**
  - Lumen Rooms … 部屋・ホールを細かく作り込みたい、長い響きがほしい
  - Elder Verb … 昔のデジタル機材の色付きの響きがほしい
  - Meld Verb … ボーカルやミックスで、自動で引っ込んでくれる実用リバーブがほしい（SMOOTHING / UNMASK）
- **聴き比べ**
  - Reference Monitor … 他人の曲（参照曲）と
  - Mix Time Machine … 自分の前のバウンスと
  - Balance Scope … トラックに挿す測定器。Plugin Link でほかのプラグインに数値を配る
- **鼻歌**
  - Idea Catcher … 思いついた瞬間を捕まえて、きれいな MIDI に（クオンタイズ・手直し → Logic へドラッグ）
- **ダブル・広げる**
  - Horizon Spread … 1 本の音のまま左右に広げる（モノにすると元の音に戻る）。歌・ギターも「広げるだけ」ならこれ（プリセット Lead Vocal - Subtle・Mono Guitar など）
  - Twin Take … 1 本録りの歌・ギターを「2 回録って重ねた」ように（音ごとにずれる別テイク。モノでも厚みが残る）
  - Echo Drift の Chorus Doubler・My Crystal の Detune Double … いつも同じずれの、加工っぽい厚み（効果として使う）
- **古っぽくする（Lo-Fi）**
  - Retro Retro … ノイズ・テープの揺れ・歪み・ビット落とし・響き・テープ劣化・エコーを重ねて、MAGNITUDE 1 本で効き具合を決める。歪みだけなら Deca Satu、揺らすだけなら My Tremolo
- **エコー**
  - Echo Drift … 普通のエコー（テープ・BBD など）
  - My Crystal … 逆再生・音程が変わるエコー・シマー
- **揺らす**
  - My Tremolo … トレモロ・オートパン・刻み / My Filter … フィルターを動かす（オートワウ・スイープ）/ Mod Host … 中のプラグインのつまみを動かす
- **サンプル・声の編集**
  - Sample Sculptor … 1 つの音を整える（切る・形・響き・逆再生）
  - Vox Chop … 声を別の声に変える・声を切って鍵盤で弾く（ボーカルチョップ）
- **ドラム**
  - Hybrid Kit … Drummer の生ドラムに 808・クラップを重ねる（生だけなら Logic の Drum Kit Designer のまま）。太さ・アタックは後ろに Fire Box・Drum Transient

## Plugin Link・GAIN MATCH・プリセット（プラグイン共通）
- 画面のいちばん下の **LINK**（OFF / 1〜4）を同じ番号にしたプラグイン同士が、裏で情報をやりとりします（例: Balance Scope → Hush Field、Fader Pilot → リバーブ、Gain Stage → Wool Fuzz / Porch Drive）
- その横の帯で、自分のプリセットの保存（SAVE）と、つまみの取り消し・やり直し（UNDO / REDO）ができます
- その帯の左端の **GAIN MATCH** を ON にすると、そのプラグインを入れても・つまみをいじっても音量が変わりません（音色だけで比べられる）。最初は OFF。
  付いていないのは、音を変えない Balance Scope、音量を変えるのが仕事の Gain Stage・Fader Pilot、音源の Sample Sculptor・AIO Guitar・Vox Chop・Hybrid Kit

## 入れ方
### プラグイン（Logic）
1. Releases の「latest」から `<フォルダ名>.zip`（例 `WoolFuzz.zip`）をダウンロードして解凍
   （または Actions の実行結果ページ下の「Artifacts」から「<フォルダ名>-macOS」）
2. ターミナルで:
   ```
   cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
   ```
3. Logic を起動 → Audio Units → MyPlugins
   出ないときは「Logic Pro → 設定 → プラグインマネージャ」で再スキャン

### アプリ
Mac の Claude Code に「<フォルダ名> を入れて」と頼むと、`tools/install-app.sh <フォルダ名>`（例 `tools/install-app.sh NumDeck`）で
Releases の latest から ~/Applications に入れます。この Mac 専用の署名をし直すので、アクセシビリティなどの許可が更新のたびに外れません。
