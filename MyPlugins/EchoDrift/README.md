# Echo Drift（エコー・ドリフト）

テープエコー・バケツリレー式（BBD）・ラジオ・コーラスなど、31 種類の「エコーの性格」を切り替えられるディレイです。
この Mac に入っている市販の定番ディレイを、オフラインで直接鳴らして測り（くり返しの時刻・音色・くり返すたびの削れ方・歪み方・揺れ方・にじみ）、
それに合わせ込んだオリジナルです。名前・見た目は別物です。

Logic Pro では **Audio Units → MyPlugins → Echo Drift**。

## 画面

### 上の段
- **スタイル**（真ん中の選択、`<` `>` で順に切替）… エコーの性格。下の表を参照
- **SINGLE / DUAL / PING-PONG / RHYTHM** … くり返し方
  - SINGLE: 左右とも ECHO 1 の時間。左右で少しだけ（数 ms）ずれて返ってくるので自然に広がる
  - DUAL: 左が ECHO 1、右が ECHO 2 の時間
  - PING-PONG: 左右を足して、左（ECHO 1）→ 右（ECHO 2）→ 左… と跳ねる
  - RHYTHM: RHYTHM の間隔で REPEATS 個のタップ。全体が 2 拍ごとに FEEDBACK でくり返す
- **PRESET** … 内蔵プリセット 12 個

### ECHO
| つまみ | 意味 |
|---|---|
| ECHO 1 / ECHO 2 | エコーの時間（1 ms〜15 秒）。下の選択で **Time**（ms）/ **Note**（音符）/ **Dotted**（付点）/ **Triplet**（3 連）。音符のときはテンポに合わせる |
| RHYTHM / REPEATS / DECAY | RHYTHM モードだけ。タップの間隔、数（1〜16）、最後のタップがどれだけ小さくなるか（0〜40 dB） |
| FEEDBACK | くり返しの量（0〜125%）。100% を超えると音がふくらみ続けるが、スタイルの歪みと天井（約 +6 dBFS）で止まる |
| MIX | 元の音とエコーの割合（等パワー。50% で両方 −3 dB） |

### TONE / MOTION
| つまみ | 意味 |
|---|---|
| SATURATION | エコーの通り道をどれだけ強く歪ませるか（0〜24 dB）。スタイルごとに歪み方が違う |
| LOW CUT | エコーの低音を削る（出口で 1 回だけ） |
| HIGH CUT | エコーの高音を削る（ループの中なので、くり返すたびにさらに暗くなる） |
| WOBBLE | スタイルが持っている揺れ（ワウ・フラッター）の量。100% = そのまま、0% = 揺れなし、200% = 倍 |
| GROOVE | 1 つおきのエコーを後ろ（＋）/ 前（−）にずらす（エコーの時間の何割か）。跳ねたリズムに |
| FEEL | 全部のエコーを後ろ / 前にずらす（1 拍の何割か）。レイドバック・前ノリ |
| INPUT / OUTPUT | 入口・出口の音量（INPUT を上げると歪みやすくなる） |
| PRIME | エコーの時間を「素数の ms」に丸める。左右や 2 つの時間がきれいに重なりにくくなり、なめらかに |

右の図: 上（オレンジ）が左、下（青緑）が右のエコー。棒の高さは 1 kHz での大きさ。
右上の小さなグラフは、1 回目のエコーの音色（オレンジ）と、くり返すたびに掛かる音色（青緑）。

## スタイル（31 種類）
| 番号 | 名前 | 性格 |
|---|---|---|
| 1 | Master Reel | 素直で広い、上まで開いたテープ |
| 2 | Studio Reel | くり返すたびに少し暗くなる、やわらかいテープ |
| 3 | Cart Echo | 中域が前に出る、上は暗め、ゆっくり不規則な音程の揺れ |
| 4 | Roller Echo | 低音が細い、速いフラッター、左右で揺れが別々 |
| 5 | Tube Reel | 中域にクセ、ざらっと歪む、細かい揺れ |
| 6 | Cheap Cassette | 細くて明るい、揺れて、つぶれ気味 |
| 7 | Bucket Swirl | あたたかく暗い BBD、ゆっくりのコーラス |
| 8 | Bucket Dark | こもった BBD、くり返すとすぐ消えていく |
| 9 | Oil Drum | 箱っぽい帯域、にじんでゴボゴボ |
| 10 | Spin Drum | 明るく、深く速い揺れ、左右が別々 |
| 11〜15 | Telephone / AM Radio / FM Radio / Shortwave / Transmitter | 電話・ラジオ系の帯域と歪み |
| 16 | Digital | きれいなデジタル（そのままくり返す） |
| 17 | Analog | 上がやわらかい、ゆっくり揺れるアナログディレイ |
| 18〜21 | Digital Chorus / Analog Chorus / Pedal Chorus / Vibrato | 揺れが主役。時間を 10〜30 ms にするとコーラス・ダブリングに |
| 22〜27 | Saturated / Fat / Distressed / Limited / Distorted / Squawk | 歪み・圧縮の性格 |
| 28〜31 | Ambient / Diffused / Splattered / Verbed | にじむ・響きになる |

## おすすめ設定
- **歌のスラップ**: Vocal Slap（Studio Reel、95 ms、FEEDBACK 8%、MIX 22%）
- **ギターの付点 8 分**: Dotted Eighth Lead（Master Reel、Dotted 1/8、FEEDBACK 38%）
- **ダブ**: Dub Tape Spin（Roller Echo、1/4、FEEDBACK 72%、SATURATION 9 dB）。再生中に FEEDBACK を 100% 超えまで上げ下げすると発振を演出できる
- **広げる**: Wide Ping-Pong（Digital、1/8 と 1/4）
- **ダブリング**: Chorus Doubler（Pedal Chorus、18 ms、FEEDBACK 0%）
- **奥に溶かす**: Ambient Wash（Diffused、DUAL 420 / 610 ms）

## Plugin Link（いちばん下の LINK 欄）
歌のトラックの **Fader Pilot** と同じ LINK 番号にすると、声が鳴っている間だけエコーを **VOCAL DUCK** の dB だけ下げ、
息つぎや休符でエコーが戻ります（言葉が埋もれない「ダッキング・ディレイ」）。

## プリセット・取り消し
いちばん下の右側で、自分のプリセットの保存・読み込みと、つまみの取り消し・やり直しができます。
