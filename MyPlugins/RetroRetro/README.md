# Retro Retro（レトロ・レトロ）

音を「古いレコード・カセット・壊れかけのテープ・昔のサンプラー」のようにするプラグインです。
7 つの加工（NOISE / WOBBLE / DISTORT / DIGITAL / SPACE / LOSS / ECHO）を重ねて、MAGNITUDE のつまみ 1 本で全部の効き具合をまとめて上げ下げできます。

## 画面
- **MAIN**: モジュールごとに ON ボタン・大きいつまみ（量）・種類やいちばん使うつまみ。右側は MASTER（MAGNITUDE・TONE・COMPRESS・WIDTH・MIX・OUT）
- **DETAIL**: 上のボタンでモジュールを選ぶと、そのモジュールのつまみが全部出ます
- いちばん下: LINK（Plugin Link）・GAIN MATCH・自分のプリセット・取り消し／やり直し
- 右上の「Factory presets」で内蔵プリセット

## モジュール
| 名前 | 何をするか | 主なつまみ |
|---|---|---|
| NOISE | サーッ・プチプチ・ハムを足す（10 種類） | TYPE、TONE（暗い ⇄ 明るい）、FOLLOW（入力の大きさについていく）、DUCK（鳴っている間は引っ込む） |
| WOBBLE | テープの回転ムラで音程が揺れる | WOW<>FLUTTER（ゆっくり ⇄ 細かい）、STEREO（左右で別々に揺れる）、速さ・形・MIX |
| DISTORT | 歪み・飽和（9 種類） | TYPE（Tube Pair・Iron Core・Crunch・Heavy・Zap・Air Pressure・Tape・Classic Pre・Pocket Recorder）、MIX、OVERLOAD |
| DIGITAL | サンプルレートとビット数を落とす | RATE<>BITS（ざらざら ⇄ ガサガサ）、SPUTTER（ビットがふらつく）、SMOOTH、DRIVE |
| SPACE | 部屋・プレート・スプリングの響き（10 種類） | TYPE、DECAY（長さ）、PREDELAY、WIDTH、DUCK。50 % を超えると元の音が減っていきます |
| LOSS | すり減ったテープ（MAGNETIC）と、低音質の圧縮音声（CODEC） | MAG<>CODEC、DROPOUTS（音切れ）、STRETCH、BLUR |
| ECHO | テープ風のエコー。くり返しの中にも揺れ・歪み・響きを入れられる | TIME（拍に合わせる / 自由）、FEEDBACK、PING-PONG、DUCK、GRAINS（くり返しの音程を変える） |
| MASTER | 全体の EQ・コンプ・広がり・テープを遅く回す VARISPEED | CUT LO / CUT HI、TONE（Tilt / Mid / Filter 1 / Filter 2）、COMPRESS、WIDTH、MIX、OUT |

- 各モジュールの FLUX … 量をときどきランダムに揺らして「生きた」古さにします
- 各モジュールの FILTER LO / HI … Focus = その帯域だけに効かせる、Cut = 効果で変わった分だけ帯域で切る
- VARISPEED … 1/2・1/4・1/8 の速さ（1〜3 オクターブ低い）。RESYNC の長さごとに元の位置へ戻ります

## おすすめの使い方
- **鍵盤・パッドを古いサンプルっぽく**: プリセット「Dusty Keys」か「Vinyl Sample」。MAGNITUDE を 50〜80 % で控えめに
- **ドラムバスを Lo-Fi に**: 「Lo-Fi Beat Bus」。COMPRESS を上げると前に出ます
- **イントロだけラジオ・電話**: 「Old Radio Vocal」「Telephone Line」をオートメーションで ON
- **センドで飛び道具のエコー**: 「Dream Echo Throw」を MIX 100 % で
- 2 ミックスに挿すときは MAGNITUDE を 20〜40 % から。LOSS の DROPOUTS は 2 ミックスでは音がポコポコするので控えめに

## GAIN MATCH
ON にすると、入れても・つまみを触っても音量が変わりません（触ったときだけ測り直して固定）。OFF なら自由。
ノイズを大きく足す設定では、ノイズの分まで含めて合わせます。MIX がほぼ 0 のときと ECHO SOLO の間は効きません。

## Plugin Link
同じ LINK 番号の Fader Pilot（ボーカルに挿したもの）が「歌っている」と送ってくる間、NOISE を NOISE DUCK の分だけ下げます（歌の隙間でだけサーッと聞こえる）。

## メモ
- 遅れ（レイテンシー）は 48 kHz で 283 サンプル（約 6 ms）。Logic が自動で合わせます
- 入力が完全に止まって 1.5 秒たつと、ノイズも止まります（トラックが鳴っていない所でサーッと鳴り続けないように）
