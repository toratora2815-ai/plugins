# AIO Effecter（エーアイオー エフェクター）0.8.0

いろいろなエフェクトを好きな順に並べて使う、ラック型のエフェクトです（Logic のトラックに挿すタイプ。AU）。
中の部品は、Guitar Rig 7 Pro の部品を 1 つずつ測って、音の動きを合わせ込んで作りました（見た目・名前はオリジナルです）。
0.1.0 は「響き」と「抑える」から 7 つの部品、0.2.0 で「抑える」の残り（コンプ・トランジェント・マキシマイザーなど）、0.3.0 でリバーブ 9 種類、0.4.0 でディレイ 6 種類（テープエコー・アナログ・ピンポンなど）、0.5.0 で EQ 5 種類と揺れもの 3 種類（トレモロ・リングモジュレーター・フランジャー）、0.6.0 でコーラス系 4 種類（昔のペダルふうフランジャー・コーラス・アンサンブル・オルガンのビブラート）、0.7.0 でフェイザー 2 種類・テープの揺れ・回るスピーカー、0.8.0 でフィルター 1 種類とワウ 4 種類（しゃべるワウも）を足しました。これから版を重ねて部品を増やします。
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

### EQ（0.5.0 で追加）
どれも、本物の周波数ごとの大きさを、つまみの位置ごとに測って合わせ込んでいます（差はおおむね 0.2〜0.5 dB）。
- **SHELF EQ** … 低い棚と高い棚だけの EQ
  - LO FREQ / LO GAIN（40 Hz〜2.1 kHz、±20 dB）、HI FREQ / HI GAIN（±20 dB）、OUTPUT（±20 dB）
  - 初期値は本物と同じで、**LO GAIN が −20 dB（低音カット）・HI GAIN が −5.5 dB** です。平らにするときは両方の GAIN を 50 % に
- **CONSOLE EQ** … 4 バンド＋ハイパス・ローパスの、ミキサー卓ふうの EQ
  - HF（棚 / BELL でピーク）、HMF・LMF（ピーク。Q は右へ回すほど広い。下げる側は細め）、LF（初期値はピーク、BELL を外すと低い棚）
  - HP FREQ（28〜364 Hz）・LP FREQ（16.8〜4.3 kHz）。どちらも 10 % より下で OFF
  - OUTPUT は 50 % で 0 dB、**初期値の 100 % は +12 dB**、0 で無音（どれも本物と同じ）
- **PARAM EQ** … 2 バンドのパラメトリック EQ
  - FREQ 1 / 2（20 Hz〜20 kHz）、GAIN（−14〜+19 dB）、Q（0 でとても鋭い）、VOLUME（初期値 −4.8 dB）
  - 全体に 16 kHz の穏やかなローパスがかかります（本物と同じ）
- **GRAPHIC EQ** … 8 本のつまみで帯域を上げ下げする EQ
  - MIN FREQ / MAX FREQ で 8 本の帯域の端（いちばん下 26〜630 Hz、いちばん上 1.1〜14.2 kHz）、RANGE で上げ下げの幅（50 % で ±14 dB）、OUTPUT
- **SCOOP EQ** … 中域をえぐるギター向けの EQ
  - TONE（左でこもる・右で細く）、FREQ（谷の位置 54 Hz〜2.3 kHz）、SCOOP（谷の深さ、最大 −25 dB）、OUTPUT

### 揺れもの（0.5.0 で追加）
- **TREMOLO** … 音量を揺らすトレモロ・オートパン
  - RATE（0.26〜16 Hz、SYNC ON で音符）、INTENSITY（深さ。100 % で切れる）、WIDTH（下がっている時間の長さ）
  - DOWN / UP（下がる・上がる角のなまり方。0 でカクカク、上げるほどなめらか）、PAN（MONO / STEREO = 左右が逆に揺れるオートパン）
- **RING MOD** … 音に別の波を掛けて、金属っぽい・ベルのような音にする
  - PITCH（掛ける波の高さ 100 Hz〜8.4 kHz）、RAW（掛ける波の倍音。上げるほど荒い）、AM/RM（AM = 元の音を残す、RM = 元の音を消す）
  - DRY/WET（初期値 0 = 素通し。本物と同じ。混ぜるには上げる）
- **FLANGER** … ジェット機のようにうねるフランジャー
  - MODE: CLASSIC（遅れ 4〜15 ms、ほどよいうねり）/ DEEP（遅れが短く、深い谷）/ RINGING（くり返しが強く、鳴くようなうねり）
  - RATE（0.1〜10 Hz、SYNC ON で音符。3.3 Hz から上は揺れが浅くなる＝本物と同じ）、WIDTH（うねりの幅）、INTENSITY（うねりの深さ。上げるほどドライの低音が下がる＝本物と同じ）
  - DEEP / RINGING では INTENSITY 0 でもうねりは強いままで、INTENSITY で主に変わるのは低音の量です（本物と同じ）
  - SYNC ON で再生中は、うねりが曲の拍にそろいます（TREMOLO も同じ。拍の頭で音が上がっている側から始まる）
  - STEREO ON で、右のうねりが左と逆向き（広がる）

### コーラス系（0.6.0 で追加）
- **ELECTRIC FLANGE** … 昔のペダルふうのフランジャー（くり返しで鳴く、やわらかいうねり）
  - COLOR（くり返し。上げるほど鳴く）、DEPTH（うねりの深さ）、RATE（0.03〜11 Hz、SYNC ON で音符）、MIX
  - STATIC ON でうねりを止めて、DEPTH の位置に固定（決まった色の「シュワッ」）。INVERT でこだまの向き、ROTATE で右のうねりを左からずらす（50 % = 同じ）
- **MULTI CHORUS** … 2 本 1 組の声を最大 3 組まで重ねるコーラス
  - MODE: CLASSIC（こもった昔のコーラス）/ LUSH（さらに暗く、速い揺れが足されて厚い）/ BRIGHT（明るく、遅れ長め）/ TWIN（素通しの明るさで、2 本が同じ向きに動く）
  - VOICES（声の数 2〜6。組が増えるほど、後ろの組は少し速く揺れて、広がりは狭い）、DELAY（声のいちばん短い遅れ 0.3〜15 ms）
  - RATE（0.25〜7.7 Hz）、AMOUNT（揺れの深さ＝音程の揺れ）、WIDTH（左右の広がり）、FEEDBACK（声をくり返して鳴かせる）、MIX、INVERT（声の向き）
  - SCATTER は本物を測った範囲では音が変わらなかったので、こちらも変えていません
- **VIBE ENSEMBLE** … 昔のコーラス／ビブラートの箱
  - MODE: CHORUS（INTENSITY で揺れの速さ 0.1〜3 Hz。深さは決まっている）/ VIBRATO（DEPTH・RATE 3.2〜11.6 Hz で音程を揺らす）。SYNC ON で音符
  - BASS・MID・TREBLE は揺れた音（ウェット）だけの音色（3 本の組み合わせを 125 通り測って合わせた）。VOLUME は全体の音量（±15 dB）、MIX、DELAY（声の遅れ）、STEREO（右の揺れを左から 0〜90° ずらす）
- **SCANNER VIBE** … オルガンのスキャナー式ビブラート（揺れながら少しこもる、独特の「ゆらゆら」）
  - MODE 1〜6（揺れの深さ）、RATE（0.75〜24 Hz）、BLEND（揺れない音を混ぜてコーラスに。0 = 揺れだけ）、COLOR（A / B / C。B・C は深い）
  - SPREAD ON で左右をずらす（WIDTH = 0〜180°）、MIX。STEREO OFF で左右の入口をまとめてから揺らす

### フェイザー・テープ（0.7.0 で追加）
- **CLASSIC PHASER** … 昔のオレンジ色の箱ふうのフェイザー（ゆったり「シュワー」）
  - RATE（0.12〜10 Hz、SYNC ON で音符。速くすると揺れは浅くなる＝本物と同じ）、DEPTH（谷の動く幅、最大 4 オクターブ）、FREQUENCY（谷の位置。上は 5 kHz あたりで頭打ち）
  - NOTCHES（段の数 2〜8。段が多いほど谷が増える。奇数の段は COLOR が低いと低音が少し下がり、COLOR を上げると逆に持ち上がる）、COLOR（くり返し。上げるほど鳴く）、DRY WET（0 でドライが +6 dB = 本物と同じ）
  - ROTATE（右の揺れを左からずらす。50 % = 同じ）、INVERT（揺れた音の向き）
- **SWEEP PHASER** … 谷が広い範囲を往復するフェイザー
  - SWEEP MIN / SWEEP MAX（谷が動く下限 73 Hz〜1.7 kHz・上限 1.9〜20 kHz）、RATE（0.01〜10 Hz）、NOTCHES（段の数 2〜10）、MIX、ROTATE（右を 0〜180° ずらす）
  - COLOR は谷の形が変わるスイッチ（まだ大まか）。MODE・INVERT は本物を測った範囲では音が変わらなかったので、こちらも変えていません
- **TAPE WOW** … 古いテープの揺れ・歪み・こもり
  - WOW（ゆっくりした音程の揺れ。TAPE SPEED で速さ 0.14〜2.8 Hz）、FLUTTER（速い揺れ 8.8 Hz）、SCRAPE（ざらっとした細かい揺れ）
  - SATURATION（テープの歪み。上げるほど潰れて小さく・暗く）、TAPE AGE（帯域が狭くなる。古いほど雑音も）、STEREO（左右の揺れを少しずらす）、MIX（等パワーで混ぜる）
- **ROTARY** … 回るスピーカー（オルガンでおなじみの「ウワンウワン」）。高音のホーンと低音のドラムが別々に回る
  - SPEED（SLOW / FAST。切り替えると T / B ACCEL の速さで加速・減速）、T FAST・T SLOW（ホーンの速さ）、B FAST・B SLOW（ドラムの速さ）
  - BALANCE（ホーンとドラムの大きさ）、PAN（ホーンとドラムの左右）、DISTANCE（マイクの距離。近いほど音量の揺れが大きい）、T / B SPREAD（左右のマイクの角度）
  - CAB（ON = キャビの音色。OFF にすると揺れも小さくなる＝本物と同じ）、MIX

### フィルター・ワウ（0.8.0 で追加）
- **MORPH FILTER** … スタジオ用のフィルター。LP（低い所だけ通す）→ BP（真ん中だけ）→ HP（高い所だけ）へなめらかに変わる
  - FREQ（角 52 Hz〜15 kHz）、RES（山の鋭さ。上げると山が立ち、全体は少し小さくなる）、SLOPE（12〜24 dB/oct）、MODE（LP / LP-BP / BP / BP-HP / HP）
- **HOWL WAH** … 昔ながらの「泣く」ワウペダル。細い山が、かかと 460 Hz〜つま先 2.2 kHz を動く
  - WAH（ペダルの位置。表示は山の周波数。Logic のオートメーションで動かす）、STEREO（OFF = 左右をまとめて 1 本で通す。ギター 1 本向け）
- **VINTAGE WAH** … 少し暗く、声のようなワウ。山はかかと 430 Hz〜つま先 2.3 kHz、高い所は控えめ
  - WAH、STEREO（初期値 ON）
- **CUSTOM WAH** … 動き方を自分で決めるワウ。かかと（MIN）・真ん中（MID）・つま先（MAX）で、周波数・山の鋭さ（RES）・山の大きさ（LEVEL）を決める
  - WAH（ペダル）、MODE（LP〜BP〜HP）、DRY（元の音を混ぜる量。0 で消える、88 % で −1.2 dB）
  - FREQ MIN / MAX（かかと 147 Hz〜2.7 kHz、つま先 470 Hz〜8.7 kHz）、RES MIN / MAX、LEVEL MIN / MAX
  - MID の 3 本は「ペダルの真ん中での値」（50 % でまっすぐ、上げるとつま先寄り、下げるとかかと寄りに曲がる）
- **VOWEL WAH** … しゃべるワウ（トークボックスふう）。WAH で母音が「ウ → オ → ア → エ → イ」と変わる
  - WAH（母音）、SIZE（口の大きさ = 1 つ目の山の高さ）、OUTPUT（±15 dB）、BRIGHT（明るく。ON で高い所が持ち上がる）、STEREO

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
- **低音と高音を少し足す**: SHELF EQ の LO GAIN・HI GAIN を 57.5 %（+3 dB）。RACKS の「Warm + Air」
- **トレモロ**: TREMOLO（初期値は 5.6 Hz・浅め）。RACKS の「Classic Tremolo」。左右にゆっくり振るなら「Auto-Pan」
- **ベルのような音**: RING MOD の AM/RM を RM、DRY/WET 50 %。RACKS の「Ring Bell」
- **ジェット機のうねり**: FLANGER の MODE を RINGING、INTENSITY・WIDTH 80 %。RACKS の「Jet Flanger」
- **昔のペダルのフランジャー**: ELECTRIC FLANGE（初期値）。RACKS の「Electric Flange」。STATIC ON で止めて使うのも定番
- **厚いコーラス**: MULTI CHORUS の MODE を LUSH、VOICES 50 %、WIDTH 85 %、MIX 70 %。RACKS の「Lush Chorus」
- **昔のアンサンブル**: VIBE ENSEMBLE の CHORUS（MIX 50 %）。RACKS の「Vintage Ensemble」。VIBRATO にすると音程が揺れる
- **オルガンのビブラート**: SCANNER VIBE（初期値 = MODE 4・BLEND 50 % のコーラス寄り）。RACKS の「Organ Vibe」。BLEND 0 で揺れだけ
- **昔のフェイザー**: CLASSIC PHASER（初期値 = 5 段・ほどよい鳴き）。RACKS の「Orange Phase」。COLOR を下げるとおとなしく、NOTCHES を 4 にすると定番の 4 段
- **ゆったり深いフェイザー**: SWEEP PHASER の NOTCHES 6 段・SWEEP を広く・RATE ゆっくり。RACKS の「Deep Sweep」
- **古いテープ**: TAPE WOW（WOW 30 %・FLUTTER 20 %・TAPE AGE 35 %・SATURATION 40 %）。RACKS の「Worn Tape」。ローファイの仕上げにはさらに Retro Retro を
- **回るスピーカー**: ROTARY（初期値 = FAST）。RACKS の「Rotary Fast」「Rotary Slow」。SPEED をオートメーションで切り替えると、ゆっくり加速・減速する

- **ワウ**: HOWL WAH の WAH を Logic のオートメーション（またはエクスプレッションペダル）で動かす。途中で止めた「半止め」の音は RACKS の「Cocked Wah」
- **しゃべるような音**: VOWEL WAH の WAH をゆっくり動かす（ウ → イ）。RACKS の「Talk Box」
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
- **SHELF EQ の初期値は低音カット（LO GAIN −20 dB）と高音少し下げ（−5.5 dB）**です（本物と同じ）。挿しただけで音が変わるので、平らにするなら両方 50 % に
- **CONSOLE EQ の初期値は OUTPUT +12 dB**です（本物と同じ）。挿すと 12 dB 大きくなるので、ふつうは OUTPUT を 50 %（0 dB）に
- EQ を大きく持ち上げたとき、本物は中で少し歪みますが、こちらは歪ませていません（細かい測定で詰めます）。CONSOLE EQ の MODE（E / G）の違いもまだです
- **GRAPHIC EQ は、MIN FREQ と MAX FREQ を近づけて（帯域を詰めて）RANGE と BAND を上げると、+100 dB 近くまで持ち上がります**（本物と同じ。帯域が重なって足し合わさるため）。
  ほかの EQ も上げきると +27〜+71 dB になります。ラックの出口の頭打ち（+24 dBFS）で止まりますが、とても大きな音になるので少しずつ
- **RING MOD の DRY/WET の初期値は 0（素通し）**です（本物と同じ）。挿しても音が変わらないときは DRY/WET を上げてください
- **FLANGER の INTENSITY を上げると、ドライの低音が下がります**（本物と同じ）。CLASSIC は 100 % で −7 dB、**DEEP は 100 % で −24 dB**（ギターの 6 弦の開放が大きく痩せます）、
  RINGING ははじめから −9 dB で、100 % で −13 dB。低音を残したいときは INTENSITY を控えめに
- FLANGER は、入る音が 0 dBFS を超えると、こだまの輪の中で少し頭打ちになります（RINGING で目立つ）。CONSOLE EQ（初期値 +12 dB）の後ろに置くときは、OUTPUT を下げてから
- **MULTI CHORUS の FEEDBACK は上げるほど鳴きが長くなります**。100 % では余韻が数十秒残ります（本物はほとんど鳴り止みません。こちらはくり返し 1 周 0.99 倍までにして、
  鳴きの強さは本物より 7 dB ほど控えめ）。鳴きの山に大きな音が入ると、とても大きく鳴るので、FEEDBACK を上げきるときは後ろに LIMITER か MAXIMIZER を
- **VIBE ENSEMBLE の BASS・MID・TREBLE は影響し合います**（本物と同じ。例: BASS と TREBLE を上げきると MID はほとんど効かない、TREBLE 0 では全体がとても小さく暗くなる）。
  3 本とも上げきると揺れた音の中低域が約 +12 dB、VOLUME も上げきると約 +27 dB になります（本物と同じ）。少しずつ上げてください
- MULTI CHORUS・VIBE ENSEMBLE・SCANNER VIBE は、本物のギター用の入口（左だけ）で測りました。ステレオの音を入れたときは、左の音から左寄りの声、右の音から右寄りの声を作ります
- SCANNER VIBE はまだ大まかな版です（揺れの幅・音色は 2〜3 dB 以内で合わせましたが、COLOR B / C の音色の違いは細かい測定で詰めます）
- **CLASSIC PHASER の COLOR を上げきると、鳴きの山が +27 dB になります**（本物と同じ）。DRY WET 0 はドライが +6 dB（本物と同じ）。
  NOTCHES が奇数（3・5・7 段。初期値の 5 段も）だと、COLOR を上げたときの山は **低音（20〜100 Hz）にも出ます**（COLOR 1 で 20 Hz +20〜26 dB。本物と同じ）。
  20 Hz より下（直流）は止めてあります。歪みの後ろで COLOR を上げきるときは、出口の音量に気をつけてください
- SWEEP PHASER の COLOR（ON）と ROTARY はまだ大まかな版です（ROTARY は左右のマイクの違いなどを細かい測定で詰めます）
- **TAPE WOW は無音でも小さなサーッという音（−66 dBFS）を出します**（本物と同じ。TAPE AGE を上げると別の雑音に変わる）。
  歪み（SATURATION）を 4 倍の細かさで計算するので、**4 サンプル（0.1 ms）遅れます**（Logic に伝えます）。MIX を 50 % 前後にすると 7 kHz あたりに谷ができます（本物と同じ）
- **ROTARY の初期値は、1.9 kHz あたりの音が大きく出ます**（キャビの山。−12 dBFS の単音が +1 dBFS まで。本物と同じ）。
  内蔵ラックの音量はギターで Orange Phase +4.3 dB、Rotary Fast / Slow +5.6 dB、Deep Sweep −0.4 dB、Worn Tape +0.7 dB です（GAIN MATCH を ON にすればそろいます）

- **ワウはペダルを一瞬で端から端へ動かしても、0.04 秒かけて動きます**（本物のペダルも一瞬では動かない。速すぎる動きでプチッと鳴るのを防ぐため）
- **ワウ・VOWEL WAH の山は大きめです**（HOWL WAH はつま先で +14 dB、VOWEL WAH は 1 つ目の山が +21 dB、CUSTOM WAH は初期値で +15〜17 dB、MODE HP で LEVEL を上げると +29〜31 dB。本物と同じ）。
  ギターに挿しただけで VINTAGE WAH・CUSTOM WAH・VOWEL WAH は +8〜9 dB 大きくなります。歪みの前に置くときは音量に気をつけてください（GAIN MATCH を ON にすればそろいます）
- **MORPH FILTER は 3 サンプル（0.06 ms）遅れます**（2 倍の細かさで計算するため。Logic に伝えます）。RES を上げきったときの山は、本物より少し低く広めです（後で詰めます）

## 入れ方（インストール）
Releases の latest から `AIOEffecter.zip`（または Actions の Artifacts「AIOEffecter-macOS」）を解凍して:
```
cp -R ~/Downloads/*/*.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
```
Logic を起動 → Audio FX → MyPlugins → AIO Effecter
