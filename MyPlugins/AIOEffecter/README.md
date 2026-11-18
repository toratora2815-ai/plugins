# AIO Effecter（エーアイオー エフェクター）0.1.0

いろいろなエフェクトを好きな順に並べて使う、ラック型のエフェクトです（Logic のトラックに挿すタイプ。AU）。
中の部品は、Guitar Rig 7 Pro の部品を 1 つずつ測って、音の動きを合わせ込んで作りました（見た目・名前はオリジナルです）。
0.1.0 は「響き」と「抑える」から 7 つの部品。これから版を重ねて部品を増やします。
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

## 入れ方（インストール）
Releases の latest から `AIOEffecter.zip`（または Actions の Artifacts「AIOEffecter-macOS」）を解凍して:
```
cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
```
Logic を起動 → Audio FX → MyPlugins → AIO Effecter
