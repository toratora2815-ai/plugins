# Hybrid Kit

生ドラムと EDM 系のドラムを部品ごとに重ねるドラム音源（Mac の Logic Pro 用 AU 音源）。
生ドラムの音は、この Mac に入っている **Logic Pro の Drum Kit Designer の録音（Producer Kit）** をそのまま読んで鳴らします。
キックとスネアには、計算で作る EDM の層（808 のサブ、クラップなど）を重ねられます。
**アタック（叩いた瞬間の輪郭）は生のまま、低音・胴は EDM** になるよう、タイミングと大きさは自動でそろえます。

- 1 本の Drummer トラックのまま鳴らせます（Drummer と同じノートの割り当て）
- 生ドラムの音のファイルはこの Mac の中で読むだけです。プラグインにも GitHub にも入っていません
- 名前・見た目は既存の製品のものではありません（EZdrummer・Superior Drummer を参考にしたオリジナルです）

## 入れ方
1. Actions の実行結果ページ下の「Artifacts」から「HybridKit-macOS」をダウンロードして解凍
   （または Releases の「latest」から `HybridKit.zip`）
2. ターミナルで:
   ```
   cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
   ```
3. Logic を起動 → ソフトウェア音源 → AU 音源 → MyPlugins → **Hybrid Kit**

Drum Kit Designer の録音が入っていないと、生ドラムは鳴りません（画面の上に「not found」と出ます）。
そのときは Logic Pro の「サウンドライブラリ」から Drum Kit Designer の音を入れてください。

## Drummer で使う
1. Drummer のトラックを作る（またはいまの Drummer トラックを選ぶ）
2. トラックの音源を **Hybrid Kit** に替える
3. そのまま再生。Drummer の演奏が Hybrid Kit の音で鳴ります

ふつうの MIDI（GM の割り当て）や、Drummer のリージョンを MIDI にしたものでも鳴ります。

## 画面
### 上
- **KIT** … Drum Kit Designer の Producer Kit から選ぶ（Studio / Modern Maple / Stadium / Tight Maple / Birch / Classic Sixties / Seventies Plexi / Pawn Shop）。
  選ぶと裏で読み込みます（数秒。右に進み具合が出ます）。読み込み中も前のキットは鳴り続けます
- **Presets** … 内蔵プリセット（下の表）

### キットの絵
- クリックで試し打ち（**shift** = 弱く、ハイハットは **alt** でオープン）。叩いた部品が光ります
- クリックした部品が下のつまみの対象になります（タムはまとめて TOMS、クラッシュとライドはまとめて CYMBALS）

### 部品のつまみ（左下）
- **VOLUME** … その部品の大きさ（全部のマイクまとめて）
- **TUNE** … 録音の音程（±12 半音）。全部のマイクが一緒に動きます
- **LENGTH** … 100 % = そのままの響き。下げるほど短く（ミュートしたような感じ）

### EDM LAYER（キックとスネアだけ）
- **BLEND** … 0 % = 生だけ
  - キック: 上げるほど、生キックの 100 Hz より下（オーバーヘッド・ルームに入った分も）が 808 のサブに入れ替わります。ビーターの当たりは生のまま
  - スネア: 強く叩いた音に EDM の層を足します。**弱い音（ゴースト）には付きません**
- **SOUND** … キック: 808 Short / 808 Long / Sub Thump / Deep Sub、スネア: Clap / Big Clap / 909 Snare / Snap
- **NOTE**（キック）… サブの音程（E1〜D#2）。曲のキーに合わせます
- **LAYER TUNE**（スネア）… EDM の層の音程（±12 半音）。左の TUNE は録音の音程

サブの鳴り始め・波の向き（極性）・大きさ、スネアの層の大きさと鳴り始め（生より 1.5 ms 後）は、録音の 1 打ごとに自動で合わせています。

### MIXER（右）
- **CLOSE** … 近いマイク（キックの中と外、スネアの上と下、タム、ハイハット）
- **OH** … オーバーヘッド（上からキット全体）
- **ROOM** … 部屋のマイク（2 種類）
- **BLEED** … かぶり（ほかの近いマイクに入った各太鼓の音）
- **OUTPUT** … 出口の大きさ

全部 0 dB が Drum Kit Designer の Producer Kit の標準のバランスです（全体は 4 dB 下げて出しています。全部を同時に強く叩いても 0 dBFS を越えにくいように）。

### SEPARATE OUTS（マルチアウト、右下）
キック・スネアなどを、Logic のミキサーの別々のチャンネルに出せます（例: キックだけに「kick back」や「キック迫力」を挿す）。
出口の番号: **1 Main / 2 Kick / 3 Snare / 4 Room / 5 Toms / 6 Hi-Hat / 7 Cymbals**（ボタンにも番号が書いてあります）

おすすめ（キック・スネアを強く加工するとき）: **2 KICK・3 SNARE・4 ROOM を ON にして、Logic の「＋」を 3 回**
1. 音源を入れるときに **「Hybrid Kit」→「Multi Output」** を選ぶ（いま Stereo で入れているなら、音源の欄をクリックして選び直す）
2. Logic のミキサーで、Hybrid Kit のチャンネルの下の **「＋」** を押す。押すたびに 2 Kick → 3 Snare → 4 Room → … の順に 1 本ずつチャンネルができます
3. **「＋」でチャンネルを作ってから**、Hybrid Kit の画面の SEPARATE OUTS で、分けたい部品のボタンを **ON**。ON にしなかった部品は Main（元のチャンネル）から出ます。
   右下の「Multi Output: 7 outs」は Logic が出口を用意している数で、「＋」でチャンネルを作ったかどうかはプラグインからは分かりません。
   チャンネルを作っていない番号のボタンを ON にすると、その部品は聞こえなくなります（たとえば 4 ROOM を ON にするなら「＋」を 3 回）
4. **Logic の 設定 → オーディオ → 一般 → Plug-in Latency Compensation（プラグインのレイテンシー補正）を「All（すべて）」**にする（「Audio and Software Instrument Tracks」のままだと、
   Kick のチャンネルに挿した Drum Transient の遅れ（9 ms）が補正されず、キックだけ遅れます）

- 分けた部品の出口には、その部品の **近いマイク・オーバーヘッド・かぶり・ルームの分が全部** 入ります（キットの中のキックだけの音）。
  出口どうしに同じ音が混ざらないので、どの出口に何を挿しても位相（波のタイミング）が崩れません。808 のサブはキック、EDM の層はスネアの出口へ
- **4 ROOM** を ON にすると、全部の部品のルームマイクをまとめて Room の出口へ（その分は各部品の出口から抜けます）。
  キック・スネアに強いコンプを掛けるときは ROOM も分けると、部屋鳴りまで持ち上がってボワつくのを防げます。ルームだけ潰して並列で混ぜる用にも
- 全部の出口を足すと、1 本（Stereo）で出したときとぴったり同じ音です。途中で ON / OFF しても 15 ms で移るので、プチッとしません
- 出口のボタンは「曲の配線」なので、内蔵プリセット・自分のプリセット・UNDO では変わりません
- Stereo で入れているときは、ボタンは OFF のままに（右下に「Stereo: …」と出ていれば ON でも Main から出ますが、
  「Multi Output: 7 outs」と出ているのに「＋」でチャンネルを作っていないと、ON にした部品は聞こえません）
- 分けた後、Main のチャンネルに挿したドラムバス用の加工は、残りの部品（タム・ハイハット・シンバル）にしか掛かりません。
  キット全体に掛けたいときは、全部のチャンネルの出力を同じバス（Aux）にまとめて、そこに挿します
- その場でバウンス（Bounce in Place）するときは「楽器のマルチ出力を含める（Include Instrument Multi-Outputs）」にチェック。しないと分けたキック・スネアが抜けます
- 「kick back」（Fire Box 入りのチャンネルストリップ設定）は、Kick のチャンネル（Aux）の Setting メニューからも選べるように、Bus のフォルダにも写しておきました
- キックだけの出口は、kick back を作ったときの素材より小さいことがあります。コンプの掛かり方が変わるので、気になったら Drum Transient の LEARN か
  Gain Stage で入口の大きさをそろえてからプリセットを読んでください

### いちばん下
- **LINK 欄**（Plugin Link）… いまは表示だけ（ほかの自作プラグインとはまだつないでいません）
- **プリセット・取り消し**（自分のプリセットの保存・読み込み、UNDO / REDO）

## 内蔵プリセット
| 名前 | キット | 中身 |
|---|---|---|
| Studio Natural | Studio Kit | そのまま |
| Pop Hybrid | Modern Maple Kit | キックの下に 808 Short（BLEND 70 %）、スネアにクラップを少し（45 %）、ルーム −3 dB |
| Trap Hybrid | Tight Maple Kit | 808 Long（100 %）、Big Clap（80 %）、ハイハット短め、ルーム −6 dB |
| EDM Punch | Studio Kit | Sub Thump（100 %）、Snap（100 %）、ルーム −8 dB、かぶり −6 dB |
| Rock Room | Stadium Kit | ルーム +3 dB、オーバーヘッド +1 dB |
| Tight Funk | Tight Maple Kit | ルームを絞って近く、タム短め、スネア少し高く |
| Classic 60s | Classic Sixties Kit | 60 年代の小さめのキット、ルーム −2 dB、タム少し短め |

## おすすめの使い方
1. Drummer トラックの音源を Hybrid Kit に → Presets →「Studio Natural」で、まず生の音を聴く
2. キックを選んで BLEND を 50〜100 % に上げ、NOTE を曲のキーの音（ベースのいちばん下の音）に
3. スネアを選んで BLEND を 30〜60 %（Clap）。もっと EDM 寄りなら 909 Snare や Big Clap
4. 部屋が多すぎれば ROOM を下げ、タイトにしたければ BLEED も下げる
- 太さ・アタックなどのミックスは、Hybrid Kit の中ではなく、後ろに挿すプラグインでやります（中にコンプ・歪みは作りません）。
  - キック・スネアだけに: SEPARATE OUTS（上）で KICK・SNARE を分けて、その出口に Fire Box → Drum Transient の自分のプリセット「キック迫力」「スネア迫力」や、
    チャンネルストリップ設定「kick back」を
  - キット全体に: Main に Fire Box の「ドラムバスを太く」（BASS 15%・THICK 25%・COMP 12 dB・SOFT CLIP 35%・LOUD 4 dB）→ Drum Transient の Presets「Drum Bus」
  - 決まった組み合わせは Logic のチャンネルストリップ設定に保存しておくと、次から 1 回で呼べます

## 分かっている限界（0.2.0）
- Vintage Kit Brushes（ブラシ）・Vintage Kit Rods（ロッド）はまだ選べません（録音の並び方がほかのキットと違うため）
- 叩く強さに対する音量の付き方は、Drummer のループの音から割り出した推定です。Drummer のハイハットの鍵盤の意味も推定です
  （確かめるため、プラグインを開いてから 10 分だけ、届いたノートを `~/Library/Application Support/MyPlugins/Hybrid Kit/midi-log.txt` に書いています。
  割り当てを確かめたら、次の版で外します）
- 生ドラムの音は Logic のファイルを直接読むので、Logic のサウンドライブラリを別のドライブに移していると読めません
