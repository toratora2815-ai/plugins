# AIO Effecter（エーアイオー エフェクター）0.2.0

いろいろなエフェクトを好きな順に並べて使う、ラック型のエフェクトです（Logic のトラックに挿すタイプ。AU）。
中の部品は、Guitar Rig 7 Pro の部品を 1 つずつ測って、音の動きを合わせ込んで作りました（見た目・名前はオリジナルです）。
0.1.0 は「響き」と「抑える」から 7 つの部品、0.2.0 で「抑える」の残り（コンプ・トランジェント・マキシマイザーなど）を足しました。これから版を重ねて部品を増やします。
音の処理は、将来 **AIO Guitar のエフェクター欄にもそのまま移せる**形で作ってあります。

## 使い方
1. Logic のオーディオトラック（ギター・ベース・声など）の Audio FX に「MyPlugins → AIO Effecter」を挿す
2. 8 段の差し込み口（スロット）の、名前の欄をクリックして部品を選ぶ
   - **音は上の段から下の段へ流れます**（例: 1 段目 NOISE GATE → 2 段目 TUBE COMP → 3 段目 ROOM VERB）
   - 左の ^ v で段を上下に入れ替え、ON で段ごとに入／切
3. 右上の **RACKS** から、組み合わせ済みのラックを読み込めます（全部の段が入れ替わります）
4. いちばん下は、ほかの自作プラグインと同じ GAIN MATCH・自分のプリセット・取り消し（UNDO / REDO）・LINK 欄

## 部品とつまみ
### 抑える（DYNAMICS）
- **NOISE GATE** … 弾いていない間の雑音を止める
  - THRESHOLD（−63〜−3 dB）: これより大きい音で開く。歪ませる前のギターなら −50 dB 前後から
  - HOLD（0.1〜30 秒）: 音が小さくなってから閉じ始めるまで。音の伸びを切りたくなければ長めに
  - ATTACK（0.1 ms〜1 秒）: 開く速さ。長くするとバイオリンのような「ふわっと」立ち上がり
  - RELEASE（1 ms〜10 秒）: 閉じる速さ。短いとピタッ、長いと自然に消える
- **NOISE REDUCTION** … 静かな所の「サーッ」を下げる
  - THRESHOLD: これより小さい所を 1:4 で下げる。上げすぎると弾いている音の尻尾まで下がる
  - DEHISS: 静かな所ほど高い音を削る量（0 で削らない）
  - RELEASE（6.5〜105 ms）: 下げ始めるまでの速さ
- **TUBE COMP** … 真空管ふうのコンプ（SATURATION で温かい歪み、そのあとで圧縮）
  - SATURATION: 真空管への押し込み（上げると音量も上がり、3 次の歪みが増える）
  - INPUT / OUTPUT（±48 dB）、THRESHOLD・RATIO・KNEE: 普通のコンプと同じ
  - ATTACK・RELEASE・DYNAMIC: 時間（DYNAMIC は強く圧縮したあとの戻りを遅く）
- **LIMITER** … 頭を天井で抑える
  - LIMIT（−40〜+40 dB）: 天井。OUTPUT（±20 dB）で上げ下げしてから天井で抑えます
  - RELEASE: 戻す速さ（dB / 秒）、HOLD: 戻し始めるまでの時間

### 抑える（0.2.0 で追加）
つまみの名前は Guitar Rig と同じ働きのものです（見た目・名前はオリジナル）。
- **TUBE PUSH** … 真空管ふうのコンプ。COMPRESS 1 本で太く・前に出す
  - INPUT: コンプへの押し込み（上げるほど強くかかる。音量はそろえたまま）、COMPRESS: かかり方（上げると小さい音が持ち上がる）
  - PUNCH ON: 音の頭を通してから抑える、DIRT ON: 真空管の歪み、MIX: 元の音の割合（0 % = 全部かけた音）、OUTPUT ±20 dB
- **TUBE PUSH XL** … TUBE PUSH に ATTACK・RELEASE・DETECT HP（低音でかかりすぎない）・SATURATION（歪み）・CHARACTER（低音と高音を持ち上げる）を足したもの
- **FET COMP** … 速い FET のコンプ（ギター・ボーカル・ドラムに）
  - INPUT: 押し込み（上げるほど強く圧縮）、OUTPUT: 圧縮した音の大きさ、RATIO: 6 段（いちばん左 ALL = 全部押しの荒い音、右へ行くほど強い。初期値はいちばん右）、ATTACK・RELEASE（右ほど速い）
  - DRY: 元の音の量。**初期値は 100 %（元の音に圧縮した音を足す「並列コンプ」）**。下げると、大きい音ほど元の音が引かれて小さくなります（本物と同じ）
- **OPTO COMP** … 光センサーのなめらかなコンプ（ボーカル・ベースに）。PEAK RED: 抑える量、GAIN: 音量（上げすぎると真空管の歪み）、MODE: COMP / LIMIT、DRY、DETECT HP
- **VCA COMP** … 素直でパンチのあるコンプ。THRESHOLD・COMPRESSION（比率）・OUTPUT・DRY・DETECT HP
- **BUS COMP** … まとめ（ドラムのバス・2 ミックス）用。ATTACK / RELEASE / RATIO は段階で選ぶ。MAKEUP・DRY・OUTPUT
- **CHANNEL DYN** … チャンネルのコンプ＋ゲート。RANGE を上げるとゲート（GATE THR より小さい音を下げる）。WET は出口の音量
- **QUICK COMP** … つまみ 3 本のかんたんコンプ。INPUT で強く、MAKEUP で音量
- **PEDAL COMP** … ギターの足元のコンプ。SUSTAIN ON で強く伸ばす。ATTACK・RELEASE は FAST / SLOW
- **TRANSIENT** … 音の頭（ATTACK）と伸び（SUSTAIN）を別々に上げ下げ。50 % で変化なし。CLIP ON で出口をやわらかく頭打ち
  - SUSTAIN は、音が減っていく所だけでなく、小さい音（−40 dB くらいより下）もずっと上げ下げします（本物と同じ）。100 % にすると −60〜−80 dB の雑音も約 +20 dB 上がり、初期値（36 %）では少し下がります
- **MAXIMIZER** … 音圧を上げて、いちばん大きい所を天井で止める（マスターや 2 ミックスの最後に）
  - INPUT（±20 dB）: 入口の音量、THRESHOLD（−20〜0 dB）: 下げるほど大きく（小さい音は THRESHOLD の分だけ上がる）、CEILING（−20〜0 dB）: 出口のいちばん大きい山
  - CHARACTER（0〜10）: 2.5 から上はきれいに止める。2.5 より下は波形を削って荒く（歪ませて）大きくする
- **VOLUME PEDAL** … 足で踏むボリューム。PEDAL（踏み込み）を Logic のオートメーションやエクスプレッションペダルで動かす
  - PEDAL: 一番上げで 0 dB、半分で HALF、一番下げで MIN の音量（その間はなめらかなカーブ）、VOLUME: 全体の音量（±20 dB）

### 響き（REVERB / DELAY）
- **ROOM VERB** … なめらかな部屋のリバーブ
  - WET: 響きの量（上げるとドライが減る）、PRE-DELAY（0〜186 ms）
  - ROOM SIZE: 響きの長さ（0.37〜4.3 秒）、BRIGHT: 高い音の残り方、TREBLE: 響きの明るさ（200 Hz〜16 kHz のローパス）
  - STEREO: MONO / STEREO
- **SPRING** … アンプについているスプリングリバーブ（「ビヨン」とした響き）
  - SIZE: 響きの長さ（0.12〜7 秒）、MIX: 量、LENGTH: ばねの長さ（最初の響きが届くまで 0〜55 ms）
  - BASS: 低い音の量と明るさの傾き、LOW DAMP / HIGH DAMP: 低い音・高い音の消え方
- **DUAL DELAY** … 左右で別々の時間のディレイ
  - TIME L / R（1 ms〜2 秒）、LEVEL L / R、FDBK L / R（くり返し）、PRE L / R（くり返しの前の遅れ、0〜2 秒）
  - CROSS: 反対側へ回す量（ピンポンっぽく）、WIDTH: 0 で左右入れ替え・0.5 でモノ・1 でそのまま、MIX: 真ん中でドライもウェットも最大

## おすすめ
- **クリーンのギター**: NOISE GATE（THRESHOLD 20 %）→ TUBE COMP（初期値）→ SPRING（MIX 45 %）。RACKS の「Spring Clean」
- **ハイゲインの刻み**: いちばん上に NOISE GATE（THRESHOLD 35 %、RELEASE 20 %）。弾き止めでピタッと止まる。RACKS の「Tight High Gain」
- **ソロに広がり**: DUAL DELAY（TIME 350 ms 前後、MIX 30 %）→ ROOM VERB（WET 35 %）。RACKS の「Room + Echo」
- **録った音のサーッ**: NOISE REDUCTION（THRESHOLD 30 %、DEHISS 60 %）。下げすぎると尻尾が消えるので、聞きながら少しずつ
- **ボーカル**: OPTO COMP（初期値）→ ROOM VERB（WET 25 %）。RACKS の「Opto Vocal」
- **ドラムのバス**: BUS COMP（初期値）→ TRANSIENT（ATTACK 65 %）。RACKS の「Drum Bus Glue」
- **太く・大きく**: TUBE PUSH（COMPRESS 35 %）→ MAXIMIZER（初期値）。RACKS の「Push + Maximize」
- **ギターのクリーンを伸ばす**: PEDAL COMP（SUSTAIN ON）をいちばん上に
- **音量をペダルで**: VOLUME PEDAL の PEDAL を Logic のオートメーション（または足のエクスプレッションペダル）で動かす
- リバーブやディレイをセンド（バス）で使うときは、WET / MIX を 100 % に

## 気をつけること
- **TUBE COMP の SATURATION・INPUT・OUTPUT は ±48 dB と幅が広い**（本物と同じ）。全部を上げると、とても大きな音になります。
  安全のため AIO Effecter の出口は +24 dBFS で頭打ちにしてありますが、少しずつ上げてください
- **TUBE COMP の RELEASE を 75 % から上にすると、下げた量がほとんど戻りません**（本物と同じ動き。こちらは最長 60 秒で戻る）。再生を止めると戻ります
- **DUAL DELAY の FDBK 100 % は、くり返しが減らずに鳴り続けます**（本物と同じ）。FDBK と CROSS を一緒に上げて合計が 100 % を超えるときは、
  鳴り続けるだけで大きくはならないように、こちらで自動で抑えます（本物は大きくなっていく）
- 部品を選び直すと、その段は 15 ms でドライに寄せてから入れ替わります（響きの残りは切れます）。AUX（センド）で 100 % ウェットにして使っているときは、
  選び直す一瞬だけドライが混ざります
- **SPRING の初期値は MIX 100 %（響きだけ）**です（本物と同じ）。トラックに直接挿すときは MIX を 30〜50 % に
- **MAXIMIZER を入れると音が 9 ms 遅れます**（先読みのため。Logic には伝えるので、再生では自動でずれを直します。録音しながら聞くときだけ少し遅れて聞こえます）。
  歪ませる部品（TUBE PUSH・FET COMP・TRANSIENT など）も 0.1〜0.2 ms 遅れます（同じく Logic に伝えます）
- **TUBE PUSH / XL の COMPRESS を上げきると、小さい音が最大 +38 dB 持ち上がります**（本物と同じ）。雑音も一緒に上がるので、上げきるときは手前に NOISE GATE を
- FET COMP の OUTPUT、OPTO COMP の GAIN、VCA COMP の OUTPUT も上げきると +20 dB 以上になります（本物と同じ）。少しずつ上げてください
- コンプを強くかける（COMPRESS・INPUT を上げる）と、無音のあとの最初の音の頭が跳ねます（本物も同じ）。大きな音を出す所では、いちばん後ろに MAXIMIZER か LIMITER を
- CHANNEL DYN のゲート（RANGE を上げたとき）は、開く・閉じるタイミングがまだ大まか（細かい測定で合わせます）

## 入れ方（インストール）
Releases の latest から `AIOEffecter.zip`（または Actions の Artifacts「AIOEffecter-macOS」）を解凍して:
```
cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
```
Logic を起動 → Audio FX → MyPlugins → AIO Effecter
