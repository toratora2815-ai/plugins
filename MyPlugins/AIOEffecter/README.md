# AIO Effecter（エーアイオー エフェクター）0.4.0

いろいろなエフェクトを好きな順に並べて使う、ラック型のエフェクトです（Logic のトラックに挿すタイプ。AU）。
中の部品は、Guitar Rig 7 Pro の部品を 1 つずつ測って、音の動きを合わせ込んで作りました（見た目・名前はオリジナルです）。
0.1.0 は「響き」と「抑える」から 7 つの部品、0.2.0 で「抑える」の残り（コンプ・トランジェント・マキシマイザーなど）、0.3.0 でリバーブ 9 種類、0.4.0 でディレイ 6 種類（テープエコー・アナログ・ピンポンなど）を足しました。これから版を重ねて部品を増やします。
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

### 響き（0.3.0 で追加）
どれも、本物の響きの「始まり・帯域ごとの長さ・量・音色・前の濃さ・左右の広がり」を、つまみ 1 本ずつ合わせ込んでいます（波形そのものは別物です）。
- **SPACE VERB** … 大きく、なめらかに揺れる空間（つまみ 16 本）
  - MODE: TIGHT / OPEN / SWELL（SWELL はゆっくりふくらむ響き）、DENSITY: LOW / HIGH（HIGH で少し濃く大きく）
  - PREDELAY（0〜2 秒、SYNC ON で音符）、FEEDBACK: 予備の遅れをくり返す（PREDELAY を上げたときだけ効く）
  - SIZE・DIFFUSION・DECAY（0.25〜20 秒くらい）・DAMP（高い音の消え方）・LOW CUT（低い音を下げる棚）・HIGH CUT
  - MOD・RATE: 響きの揺れ、REVERB: 0 % で予備の遅れのこだまだけ・100 % で響きだけ、MIX、FREEZE: 響きを鳴らしっぱなしに
- **CLASSIC VERB** … 昔のデジタルリバーブ 8 種類（ROOM / HALL / PLATE / BIG PLATE / CHAMBER / BIG HALL / MONO 1 / MONO 2）と MIX だけ
  - MONO 1・2 は左右が同じ響き、MONO 2 は 8 kHz から上がない暗い響き
- **SHAPE ROOM** … 形を変えられる部屋（つまみ 16 本）
  - WET・DRY（別々の量）、SIZE（響きを伸び縮み）、START（響きの頭を飛ばす＝遠く暗く）、DECAY、DEPTH（上げると遠くに）、PREDELAY（0〜1 秒）
  - REVERSE ON: 響きを逆回し（約 1.8 秒かけてだんだん大きくなって切れる。響きは 1.8 秒遅れて来ます）
  - LOW / PEAK / HIGH の GAIN・FREQ（PEAK は Q も）: 響きの音色。GAIN は ±12 dB、50 % で 0 dB
- **PICK ROOM** … 8 つの部屋から選ぶ（BOOTH / ROOM / STUDIO / CHAMBER / HALL / PLATE / CHURCH / CAVE）。DECAY で長さ、LOW CUT、MIX
- **CLUB VERB** … シンプルな大きいリバーブ（盛り上げ・ブレイク向け）。SIZE（0.5〜8 秒）、LP・HP（響きの明るさ・低音の長さ）、MIX、FREEZE
- **DIGI HALL** … 80 年代はじめのデジタルホール。ALGORITHM 3 種類、INTENSITY（長さ）、BASS / MID / X-OVER（低音・中音の響き方）、
  HI CUT・REV COLOR（明るさ）、DEPTH、PREDELAY（3〜131 ms）、MODULATION（0〜3）、MIX、INPUT / OUTPUT（±10 dB、0 で無音）

### ディレイ（0.4.0 で追加）
どれも、本物の「こだまの時刻・大きさ・音色・くり返しの減り方・左右の分かれ方」を、つまみ 1 本ずつ合わせ込んでいます。TIME を変えたときは、読む位置を 40 ms でつなぎ替えます（プチッとしない。BBD ECHO と TAPE HEADS は本物どおり音程がすべる）。
- **WARP DELAY** … こだまを逆回しにしたり（REVERSE）、くり返すたびに音程を変えたり（PITCH）できるディレイ
  - TIME（5 ms〜2 秒、SYNC ON で音符）、FEEDBACK、WET（エネルギーで混ぜる）、ST TIME（右を短く）、CROSS ON で左右に交互（初期値）
  - REVERSE / ST REVERSE（右だけ逆回し）、DETUNE（2 回目からのくり返しを少しずらして厚く）、PITCH（±1 オクターブ）
- **BBD ECHO** … 暗いアナログのディレイ（コーラス・ビブラートつき）。FEEDBACK を上げると発振して鳴り続けます
  - INPUT（−20〜+20 dB。ドライにもかかる）、TIME（28 ms〜1 秒）、MIX、TREBLE（こだまの明るさと大きさ）、BASS
  - DEPTH・MODE（CHORUS / VIBRATO）・RATE、ACCEL（TIME を変えたときの追いかけの速さ）
  - 小さい音ほどくり返しが早く消えます（昔の機材の癖を再現）
- **WIDE DELAY** … 揺れで広がるステレオディレイ。DIFFUSION ON でくり返しが雲のようににじむ
  - TIME（5 ms〜2 秒）、FEEDBACK、WET、TREBLE・BASS（こだまの音色、輪の中にもかかる）、MOD RATE / DEPTH、INVERT（ウェットの極性）
- **CLUB DELAY** … テンポに合わせる DJ 向けディレイ
  - TIME（1 小節〜1/32 の 7 段）、SPREAD ON で右だけ 1 段長く、FREE ON で ms
  - FILTER（真ん中で素通し、左でこもる・右で細くなる）、FEEDBACK 100 % で減らずに鳴り続ける、FREEZE で今のこだまを止めて回し続ける
- **STYLE DELAY** … 5 つの色（MODERN / ANALOG / TAPE / DIGITAL / DIFFUSE）を選べるテンポ同期ディレイ
  - TIME（TIME MODE が NOTE で 1/64〜1 小節の 7 段、DOTTED・TRIPLET、MS）、FEEDBACK（83 % から上は鳴り続ける）、MIX
  - STEREO MODE: 0 = モノ、1 = 左右を少しずらす（初期値）、2 = 右を 2 倍（ピンポン）
  - LO CUT / HI CUT（12 dB/oct）、DEPTH・RATE（揺れ）、CHARACTER、DUCK（弾いている間はこだまを下げる）
- **TAPE HEADS** … ヘッドが 4 つあるテープエコー＋スプリング
  - HEAD A / HEAD B: どのヘッドで鳴らすか（1〜4。2 なら基本の 2 倍の遅れ、OFF で鳴らさない）
  - HEAD MIX（0 で A だけ・1 で B だけ）、STEREO（左右で A と B の混ぜ方を広げる。0 でモノ）
  - SPEED: テープの速さ（SYNC ON で 1/8〜1/32 の 7 段、OFF で 60〜240 ms）。速いほど明るく、遅いほど暗いこだま。動かすと音程がすべる
  - FEEDBACK（80 % を超えると鳴り続けて、テープの頭打ちで止まる）、ECHO VOL（こだまの量。ドライはいつも 100 %）
  - ECHO BASS / ECHO TREB: テープの中の音色（くり返すたびに強くかかる）。BASS / TREBLE: こだまの出口の音色（1 回だけ）
  - WARBLE: テープの揺れ（70 % まではわずか、上げきると大きく揺れる）、HEADROOM: 下げるほどテープが早く頭打ち（太く・つぶれる）
  - REV VOL / REV TIME: スプリングの量と長さ（0.3〜10 秒）

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
- **大きな空間**: SPACE VERB（PREDELAY 20 ms、DECAY 少し長め、MIX 30 %）。RACKS の「Big Space」
- **昔のプレート**: CLASSIC VERB の PLATE（MIX 18 %）。RACKS の「Vintage Plate」
- **逆回しのふくらみ**: SHAPE ROOM の REVERSE ON（WET 50 %）。RACKS の「Reverse Swell」
- **80 年代のホール**: DIGI HALL（MIX 25 %）。RACKS の「80s Hall」
- **テープエコー**: TAPE HEADS（初期値。3 連 8 分と 1/4 のヘッド＋少しのスプリング）。RACKS の「Tape Echo」。ギターなら HEAD MIX 50 %、WARBLE 75 % で揺れを足す
- **暗いアナログのディレイ**: BBD ECHO（初期値）。RACKS の「Analog Echo」。FEEDBACK を上げると発振していくので、オートメーションで盛り上げに
- **ピンポン**: STYLE DELAY の STEREO を PING-PONG、TIME 1/8。RACKS の「Ping-Pong 1/8」
- **DJ ふうのこだま**: CLUB DELAY（初期値は 1/4 と右が 1 段長い）。FILTER を左右に回して音色を変える、FREEZE でこだまを止めて回す。RACKS の「Club Echo」

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
- 響きの部品の **MODE・PRESET・ROOM・ALGORITHM を切り替えると、響きが一瞬（約 50 ms）小さくなって残りが切れます**（プチッとしないため）
  FREEZE 中に切り替えたときは、FREEZE を外したときに切り替わります（止めている響きを消さないため。外した瞬間に止めていた響きは約 15 ms で消えてから切り替わります）
- **FREEZE で何分も止めていると、響きは少しずつ小さくなります**（10 分で 10〜40 dB ほど。発振しないための安全の分）
- **SHAPE ROOM の REVERSE ON は、響きが約 1.8 秒遅れて来ます**（逆回しのため。Logic への遅れの申告はしません。ドライはそのまま）
- **SPACE VERB の FEEDBACK は PREDELAY を上げたときだけ効きます**（本物と同じ）。PREDELAY を長くして FEEDBACK を上げきると、こだまがなかなか消えません
- FROST VERB・EIGHT VERB・DIGI HALL XL の初期値は響きが多め（本物と同じ）。トラックに直接挿すときは MIX を下げてください

- **TAPE HEADS の SPEED・BBD ECHO の TIME を動かすと、こだまの音程がすべります**（本物のテープ・アナログ機材と同じ。BBD は ACCEL で速さを変えられます。
  すべる速さは音程 0.1〜4 倍まで = 逆回りはしない）。
  ほかのディレイの TIME は、読む位置を 40 ms でつなぎ替える（音程はすべらない）
- **TAPE HEADS の FEEDBACK は 80 % を超えると鳴り続けて、テープの頭打ちで止まります**（本物と同じ）。HEADROOM を下げるほど早く頭打ちになり、こもった音に。
  音色を上げて発振させると、こだまは最大 +6 dBFS ほどで鳴り続けます（STYLE DELAY の FEEDBACK 83 % から上も同じ）。後ろに LIMITER などを
  ECHO BASS・ECHO TREB はくり返すたびに強くかかるので、上げすぎると発振しやすくなります
- TAPE HEADS は本物のつまみ 20 本のうち、DROPOUTS・NOISE・ACCELERATION・SPRING LENGTH を外して 16 本にしました（本物の初期値のまま。古っぽさを足すなら Retro Retro を後ろに）
- BBD ECHO の INPUT は −20〜+20 dB で、ドライにもかかります（本物と同じ）。上げきるととても大きな音になります

## 入れ方（インストール）
Releases の latest から `AIOEffecter.zip`（または Actions の Artifacts「AIOEffecter-macOS」）を解凍して:
```
cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
```
Logic を起動 → Audio FX → MyPlugins → AIO Effecter
