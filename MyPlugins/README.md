# MyPlugins の一覧

Mac の Logic Pro で使う、自作のプラグイン（AU）と Mac アプリ（.app）です。
GitHub に push すると自動でビルドされ、Releases の「latest」に置かれます。くわしい使い方は、各フォルダの README を見てください。

## プラグイン（AU。Logic の「Audio Units → MyPlugins」、Sample Sculptor・AIO Guitar・Vox Chop は音源の欄）
| 名前 | フォルダ | 何用か |
|---|---|---|
| Wool Fuzz | WoolFuzz | ビッグマフ系の太いファズ |
| Porch Drive | PorchDrive | ブルース系オーバードライブ（BD-2 系の味） |
| Lumen Rooms | LumenRooms | 部屋・ホールを細かく作り込むリバーブ。長い響きも |
| Elder Verb | ElderVerb | 70〜80 年代のデジタルリバーブ機材ふうの、色の付いたリバーブ |
| Meld Verb | MeldVerb | EARLY / PLATE / HALL を三角形で混ぜる、自動で引っ込む実用リバーブ |
| Echo Drift | EchoDrift | テープ・BBD・ラジオなど 31 種類の性格を持つディレイ |
| Horizon Spread | HorizonSpread | モノラルで崩れないステレオの広げ役 |
| Hush Field | HushField | 耳に刺さる響きを鳴った瞬間だけ下げる（動的レゾナンス抑制） |
| Forge Strip | ForgeStrip | EQ・コンプなど 11 モジュールのチャンネルストリップ |
| Gain Stage | GainStage | トラックの入口の音量をワンクリックでそろえる |
| Fader Pilot | FaderPilot | ボーカルの音量を自動でフェーダーを動かして揃える |
| Balance Scope | BalanceScope | 帯域バランスとラウドネスの測定器。Plugin Link の配り役 |
| Mod Host | ModHost | 好きな AU を中に入れて、どのつまみでも LFO などで動かす |
| Sample Sculptor | SampleSculptor | サンプルを落として加工し、ドラッグで Logic に戻す（AU 音源） |
| AIO Guitar | AIOGuitar | 自分のエレキを録った音で鳴らす全部入りギター音源（AU 音源＋単体アプリ） |
| Vox Chop | VoxChop | 自分の声を別の声（女性・男性・中性・子ども・ささやき・ロボット…）に変えて、切って鍵盤で弾く（AU 音源） |

## アプリ（.app）
| 名前 | フォルダ | 何用か |
|---|---|---|
| Reference Monitor | ReferenceMonitor | 自分のミックスと他人の曲（参照曲）を聴き比べる |
| Mix Time Machine | MixTimeMachine | 自分の前のバウンスと聴き比べる（バウンス履歴） |
| Idea Catcher | IdeaCatcher | 思いつきの鼻歌を録って、キーとテンポ入りの MIDI に。できた MIDI は Logic へドラッグ（メニューバー常駐） |
| Sound Sketch | SoundSketch | 口マネで手元のサンプルを探す |
| Num Deck | NumDeck | テンキーに Logic などの操作を割り当てる |
| Aside | Aside | ひとりごとをリマインダー・カレンダーに入れる秘書 |

- `Shared` … プラグイン共通の部品（Plugin Link、プリセット保存・取り消しの帯）。単体では使いません

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
- **サンプル・声の編集**
  - Sample Sculptor … 1 つの音を整える（切る・形・響き・逆再生）
  - Vox Chop … 声を別の声に変える・声を切って鍵盤で弾く（ボーカルチョップ）

## Plugin Link・GAIN MATCH・プリセット（プラグイン共通）
- 画面のいちばん下の **LINK**（OFF / 1〜4）を同じ番号にしたプラグイン同士が、裏で情報をやりとりします（例: Balance Scope → Hush Field、Fader Pilot → リバーブ、Gain Stage → Wool Fuzz / Porch Drive）
- その横の帯で、自分のプリセットの保存（SAVE）と、つまみの取り消し・やり直し（UNDO / REDO）ができます
- その帯の左端の **GAIN MATCH** を ON にすると、そのプラグインを入れても・つまみをいじっても音量が変わりません（音色だけで比べられる）。最初は OFF。
  付いていないのは、音を変えない Balance Scope、音量を変えるのが仕事の Gain Stage・Fader Pilot、音源の Sample Sculptor・AIO Guitar・Vox Chop

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
