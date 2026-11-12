# Noise Out — 作業メモ（Claude 用）

録った音の雑音・ハム・部屋鳴りを取る。本人の Mac の iZotope RX 12（試用版、2026-11-11 時点で期限あり）に音を通して測り、合わせ込んだ。
2026-11-11 本人「RX12 の音の響きとか抑制とか、そのまんま同じものを作って。名前は noise out」。
ミックスエンジニア係: 「18 個まるごとは無理（Dialogue Isolate・Music Rebalance・De-bleed・Repair Assistant は機械学習で測っても写せない）。
形を変えれば価値あり = ミックスで使う 3 つ（NOISE / HUM / ROOM）を 1 枚に、Fader Pilot 連携」。本人が「3 つに絞る」「響き = 部屋の響き」を選んだ。

## ID・コード
- PLUGIN_CODE `Nzot`、Plugin Link の Kind 24（PluginLink.h には未登録。23 = My Crystal）
- パラメーター（版のヒント 1）: humOn（既定 OFF）, humFreq（Auto / 50 Hz / 60 Hz）, humReduction（0..60 dB、既定 60）, humHarmonics（1..16、既定 8）,
  noiseOn（既定 ON）, noiseMode（Adaptive / Learned）, noiseReduction（0..30 dB、既定 12）, noiseThreshold（−20..+10 dB、既定 0）,
  roomOn（既定 OFF）, roomReduction（−10..20、既定 10）, roomTail（0.5..4 s、既定 1.0、skew 0.6）, output（−24..+12）, delta, gainMatch（版 10）, linkGroup
- 状態に保存（apvts.state のプロパティ）: noiseProfile（LEARN の形。23.4375 Hz おき 1025 点の dB をカンマ区切り、サンプルレートによらない）、humF0（AUTO で最後に見つけた周波数）
- LEARN はパラメーターにしていない（画面のボタン → processor.setLearning）。3 秒（鳴っている所だけ数える）で自動で終わり、プロセッサーのタイマー（10 Hz）が noiseMode を Learned にして形を保存
- 遅れ 2048（44.1/48 kHz）、4096（88.2/96）、8192（176.4/192）。どのモジュールの ON/OFF でも変わらない

## 仕組み（NoiseOutDSP.h、JUCE なし）
- 入口 → HUM（時間領域） → STFT（√ハン、N = 2048 @48k、ホップ N/8）で NOISE と ROOM の下げ量（dB）を足して 1 回でかける → OUTPUT
- 左右は同じ下げ方（左右の平均の大きさで判定）。DELTA = N サンプル遅らせた元の音 − 出口（HUM で取った分も入る）
- 全部 OFF なら遅れ N の素通し（誤差 −137 dB）
- tools/render_noiseout.cpp: ファイルを通す CLI（T.名前=値 で Tuning を上書き、learn=a:b、freeze=a:b、comp=1 で遅れを詰める）

## 測ったこと（build/noiselab、コミットしない）
- 測定ホスト rx2（Swift、AVAudioEngine オフライン、out-of-process で開く。パラメーターは parameterTree の address）。RX の AU は全部 aufx・iZtp。
  rZD2 = Voice De-noise（遅れ 0）、rZN2 = Spectral De-noise（遅れ 2047、Adaptive ON で 75263 = 学習時間ぶん先読み）、rZH2 = De-hum（遅れ 0）、rZV2 = De-reverb（遅れ 9727）
- 信号は gen.py（say の声 Kyoko + Samantha、ピンク・白・茶・ファン風の雑音、ハム、合成した部屋の響き RT 0.6/1.2/2.5 秒）。出力は rxdata/<mod>/<信号>__<設定>.f32（左だけ、2.5 秒長い）、遅れは rxdata/lat.json
- Spectral De-noise の Learn はパラメーターにないので測れない（Adaptive だけ測った）
- De-hum の Dynamic モード（初期値）は、このホストではどの信号・設定でも出力が入力とビット単位で同じだった（何もしない）。Static だけ合わせた

### HUM
- RX の Static = RBJ のピーキング EQ（A = 10^(gain/40)、Q = Filter Q）を f0 × k に並べたもの。−3 dB 幅 = f/(A·Q)（−60 dB・Q 1000 で 60 Hz なら 1.9 Hz）。
  直流カットは 1 次 1.5 Hz（5 Hz で −0.37 dB）。自作は倍精度の双 2 次で同じ式 → インパルス応答の差 −80 dB（s_def・g20・b50・h4・h16）
- AUTO（自作）: 1.2 kHz の 4 次で落として 3 kHz に間引き、8192 点（2.7 秒）の FFT を 0.25 秒ごと。**50 / 60 Hz ±0.3 Hz** を 0.01 Hz 刻みで、
  倍音 8 本の「細い山（±1 ビンの最大）− まわり ±3〜12 Hz の中央値」の和が最大の所。倍音ごとに ±0.35 Hz の本当の山（両隣より高く、窓の端でない）を放物線で補間して重み付き平均。
  採用の条件: 12 dB 以上の倍音が 2 本以上（か 24 dB 以上が 1 本）、基本か 2 倍に山、細かく求めた値も ±0.32 Hz の中、3 回続けて 0.08 Hz 以内・大きさ ±4 dB 以内。
  2 秒見つからなければ外す。見つからない間はノッチを 0 dB に（何も削らない）。鳴り始めから約 2 秒で見つかる。
  最初は 45〜65 Hz を探していたが、残響の多い声（Samantha の平らな音程の尾）とベースの G1・B1 を一時的にハムと見なしたので絞った（build/noiselab/test/humfalse.cpp）
- 解析は音のスレッドの山を作らないよう段に分けた（FFT → 中央値の表を 16 回に分割 → 判定。ブロックごとに 1 + ブロック長/128 段）。HUM ON かつ AUTO のときだけ解析し、間引きはいつも

### NOISE（Voice De-noise に合わせた）
- 初期設定（Adaptive・Reduction 12・Dialogue・Surgical）で、雑音だけの所は −12 dB（ちょうど Reduction）。ただし帯域で違う:
  400 Hz 未満は浅め（−9〜−10）、9〜13 kHz は 1.15 倍、16 kHz より上は 0.7 倍（REDUCTION に比例 → 下げ量に周波数の倍率 nHfW をかけた）
- 手動しきい値（Adaptive OFF）の階段で: 「しきい値より x dB 大きい」→ 下げ量の曲線は R によらず同じ形で、R で頭打ち（R 12 と 20 で高い所が一致）
- Adaptive は持続音を少しずつ雑音として覚える（8 秒伸ばした母音が −0.8 → −7.2 dB）。自作の推定も上がる速さを制限して同じ動き
- 合わせ方: 1/3 オクターブ帯 × 10 ms ごとの下げ量（dB）を本物と比べ、平均の差を Nelder-Mead で小さくした（fitnoise2.py）。
  声＋雑音 4 種・持続音・雑音の段差・断続音 × Reduction 6/12/20、Threshold ±5/±10、雑音だけ 4 種
- Music・Gentle は 1.1.0 で合わせた（下の 1.1.0 の節）。Spectral De-noise とは帯域ごとの動きがかなり違う（平均 4〜5 dB）ので別物

### ROOM（De-reverb に寄せた）
- 本物の特徴: 直接音の間は下げない（クリックなら 80〜100 ms 後から、0.2 秒の雑音なら終わってから）。下げ量は −24 dB で頭打ち（どの Reduction でも）。
  Reduction 5 / 10 / 15 / 20 の平らな所 −3.3 / −9.2 / −14 / −17（RT 1.2 秒の響き、Tail 1 秒）。マイナス −5 / −10 で +2.5 / +8.3 dB（響きを足す）。
  Tail を長くすると後ろほど深く（Tail 4 で −24 まで）、短いと浅い。下げるのは直接音から一定の時間（Tail 1 で約 1.3 秒、2 で約 1.8 秒）まで。
  実際の響きが速く減るほど深く下げる（RT 0.6 で −21、1.2 で −9、2.5 で −4）
- 自作: 1/12 オクターブの帯ごとに、「ゆっくり減る包絡（rEnvDbs）を rDryK 倍より超えた所 = 新しく鳴った直接音」だけを履歴に入れ、
  窓（rWinA + rWinB × TAIL 秒、Td より前）の中で「直接音 × 予想の減り方（60 dB を rRate·TAIL^rRateP 秒で）」の最大を今の響きの予想 λ にする（帯ごとの単調な列で O(1)）。
  q = μ·λ / 今の大きさ、μ = rMu0·(R/10)^rMuP、下げ量 = min(24, rA·(R/10)·((q − q0)/(1 − q0)))（本物は声では R² に、響きだけの所では R に比例して深くなる）。
  R がマイナスは上げ量 = rBoostK·|R|·q'。帯の下げ量を時間でならして、ビンへ直線補間 → 周波数方向にならす
- 予想に「下げた後の音」や「そのままの音」を使うと、響きの尾が自分を響きと見なし続けて、本物が止める所でも下げ続けた（試してやめた）

## DSP 検査係（2026-11-12）で直したこと
- 1 回目の判定「危ない」。直したこと:
  - LEARN の形が「開いて再生せずに保存」で 0 dB に壊れた（setProfile が音のスレッド用の配列にしか書かず、保存は古い publishedProfile を読んでいた）→ setProfile が publishedProfile（atomic）に直接書く。prepare でも作り直す
  - Fader Pilot の freeze が Reset 直後の最初のフレームから来ると、推定が 200 dB のまま固まって −R dB 下がりっぱなし → 最初のフレームの初期化を freeze の外へ
  - HumFinder が HUM OFF でも 0.25 秒ごとに 0.7〜1.1 ms → 解析は HUM ON・AUTO のときだけ、段に分割。48 kHz・32 サンプルで 1 回の最大 96〜115 µs（STFT の 1 フレーム分）
  - HUM の ON/OFF・倍音の数の変更で段差（−13〜−34 dBFS）→ humMix と倍音ごとの hw[h] を 40 ms で混ぜる。0 になったらノッチの状態を消す
  - Logic の Reset（再生開始・位置の移動）で ADAPTIVE の推定と AUTO のハムを忘れていた → 推定・下げ量の初期化は prepare だけ、Reset は音のバッファ・ROOM の履歴・ノッチの状態だけを消す
  - DELTA の切り替え 15 ms のクロスフェード、入口の NaN・無限大を 0 に、LEARN は押した回数（learnStart / learnStop）で渡す（フラグとラッチをやめた）、TailTime 2 秒
  - 重さ: log10・exp を vForce でまとめて、画面用は 4 フレームに 1 回。CPU 48 kHz 0.94 %・192 kHz 3.2 %（1 コア）。音は前と −138 dB 以下の差
- 2 回目の判定「直してから」: GAIN MATCH が NaN を 0 にする前の入口を測っていて、測定中に NaN が 1 サンプル来ると出口がずっと NaN・gainMatchDb="nan" が保存された
  → processBlock の頭（measureInput より前）で非有限値を 0 に。ほかに: DELTA の混ぜる量を double で端まで、曲頭から Fader Pilot が「歌っている」間は推定を始めず ADAPTIVE で下げない、
  低い倍音のないジー音（強い倍音 4 本以上）も AUTO で見つける、HUM の混ぜる量を 1 サンプルずつ（32 サンプルの階段をやめた）、prepareToPlay ではハムの種だけ入れる（LEARN の形は engine が自分で作り直す）
- GainMatch.h（Shared、15 個が使う）も NaN に弱い（入口の非有限値・curDb/targetDb・loadFrom の "nan"）。Shared を次にまとめて変えるときに直す
- 分かっている限界: 192 kHz で Logic のバッファ 32 サンプルにすると、STFT の 1 フレーム（1024 サンプルごと）の 1 回が 232 µs で持ち時間 167 µs を超える（64 サンプルなら 52 %）。
  HUM の REDUCTION を浅く（10 dB）すると、止めた後のノッチの鳴き残りが TailTime 2 秒を超える（2 秒後 −22 dBFS、10 秒後 −47 dBFS、0.3 のハムで）。ちょうど 100 / 120 Hz の正弦波はハムとみなす

## 1.1.0（2026-11-12）「全部できる限り本物に近づけて」
本人: 「全部できる限り本物に近づけて」。ミックスエンジニア係: 「形を変えれば価値あり = つまみを全部まねるのでなく効き方を寄せる。DETAIL に細かいものをしまう」。
本人の選択: 効き方を寄せる / 新しく Mouth De-click・Breath Control・De-plosive を 4 段目に（初期 OFF、つまみ 1〜2 本）/ 測る素材は合成だけ（本人の録音は使わない）。
やらないと決めたもの（係）: Spectral De-noise の型、遅れゼロのモード、HUM の FREE・HP/LP・倍音ごとの傾き、ARTIFACT SMOOTHING・ENHANCE DRY のつまみ

### 増えたパラメーター（版のヒント 2。Logic の番号をずらさない）
- noiseOptimize（Dialogue / Music、既定 Music）、noiseGentle（既定 OFF、DETAIL）
- roomBandLow / roomBandLowMid / roomBandHighMid / roomBandHigh（0..10、既定 6 = 本物の初期値、DETAIL）
- humWidth（Q 300..2000、既定 1000 = つまみの真ん中、DETAIL）
- mouthOn・mouthSens（0..10、既定 4）・mouthSkew（−10..10、DETAIL）/ breathOn・breathReduction（0..40 dB、既定 12）・breathSens（DETAIL）/
  plosiveOn・plosiveStrength（0..10、既定 5）・plosiveSens（DETAIL）・plosiveFreq（50..500 Hz、既定 200、DETAIL）
- DETAIL に入るものと段は NoiseOutProcessor::detailParams()。画面は「初期値から動いていたら DETAIL に点」「ダブルクリックで段の DETAIL だけ初期値へ」（ジェスチャー付きなので GAIN MATCH が測り直し、UNDO にも入る）
- 状態に noiseOutVersion = 2 を書く。**ないもの（1.0.0 で保存した曲）は noiseOptimize を Dialogue にして読む**（1.0 と同じ NOISE の音のまま）。ROOM は方式が変わったので 1.0 の曲でも効き方が変わる

### 仕組みの変更（NoiseOutDSP.h）
- 遅れ = N + 16·H（48 kHz で 2048 + 4096 = 6144 = 128 ms）。分析したフレームを 16 枚の輪に入れ、NOISE・ROOM の下げ量と一緒に 15 フレーム後に取り出してかける。
  その間に息・吹かれを「先まで見て」決める（voiceFixFrame）。乾いた音のバッファも dryLen = N + LA
- 無音（全帯域のパワー 1e-13 未満）のフレームでは ADAPTIVE の推定を止める（作り直しの途中、曲頭のデジタル無音で推定が −190 dB に張り付き NOISE が効かなくなったため）
- MOUTH: 時間のまま（HUM の後・STFT の前、先読み H）。高域（mFcBase × 2^(SKEW/5)）の 2 次ハイパスの包絡が「まわり（mBaseMs）× しきい値」を超え、自分の山から mEndDb 下がるまでが mMaxMs 未満ならピチャ音。
  その範囲（前後 mMargMs のなめらかな窓）だけ、直線位相の FIR ローパス（mRepHz × 2^(SKEW/10)、48 kHz で 255 タップ）で分けた高域を抜く
- BREATH: フレームごとの特徴（300〜6000 Hz の大きさ、250 Hz 未満の割合、500〜6000 Hz の平らさ、重心）で「息らしい」を決め、前後に続く長さが bMinMs 以上なら下げる。
  深さは −bSat·tanh(REDUCTION / bSat)（本物は −100 dB にしても −26 dB 前後で止まる）
- PLOSIVE: 吹かれの境目より下（plosiveFreq、pWidth オクターブでなだらかに）の大きさが「ふだん（pBaseMs で追う）と 500〜4000 Hz」より pThr 以上出たら、先 pAhead フレームまでの最大で下げる。
  深さ min(pMaxDb·k/2, 出た量·pSlope·k)、k = (STRENGTH/5)^pStrP
- ROOM: 予想を足し算の形に（Habets の後期残響の推定。λ = a·λ + c·D(t − fd)、D = max(0, P − κ·λ)、a は TAIL から）、下げ方を log10(μ·λ/P) のロジスティックに（rEst = 1、rLaw = 1）。
  前の方式（窓の中の最大を減らす）の並べ直しは rEst = 0 のときだけ動く。帯域の強さは μ に (s/6)^rBandP をかける（境目 rX1〜3 はロジスティックでなだらかに）
- 画面用: engine.mouthClicks（直したピチャ音の数）・breathCutDb・plosCutDb

### 合わせ込み（build/noiselab、コミットしない）
- 追加で測ったもの: rxdata/mouth（52）・breath（45）・plosive（36）、vdn の music / gentle、room の帯域（b0・b3・b10・blo・blm・bhm・bhi・blo0・blm0・bhm0・bhi0）。全部で 1482 通り
- 声の試験音は gen.py の合成（say の声に作ったピチャ音・息・吹かれを足したもの。正解の位置は sig/truth_new.json）
- 道具: fitnoise5.py（music / gentle）、fitroom5.py（ROOM の方式）、fitroomband.py（帯域）、fitvoice.py（mouth / breath / plosive）、
  verifyvoice.py・verifyroom.py（本体に書いた値で同じ差になるか）、bake.py（json → Tuning の初期値）
- **注意: scipy の Nelder-Mead は、最初の値が 0 の座標を 0.00025 しか動かさない。log で持っている値（指数など）を 1.0 から始めると、そこから動かなかった**
  （rAP・rGamma・rHk・pStrP・nMusRel・nMusKnee・nGenRel が 1.000x のまま）。initial_simplex を自分で作ってやり直した（fitnoise6.py・fitroom6.py・fitvoice2.py）

### 1.1.0 で分かっている限界
- MOUTH: 本物はピチャ音ごとに「取る・取らない」の選び方が違う。感度を下げる（2）・高い側に寄せる（SKEW +5〜+10）と、本物は 2.5 kHz より低い山のピチャ音と、
  大きいピチャ音（−18 dBFS より大きい）を取らなくなるが、自作は全部取る。声の中のピチャ音（m_speech_clicks）は、本物も自作も 60 個中 10 個前後を取るが、選ぶものが半分ほど違う。
  大きいピチャ音を本物は −25〜−34 dB まで下げる（つなぎ直していそう）、自作は高域だけ抜くので −15〜−25 dB
- 試験音（合成）では、声のない所・きれいな声・歌にはほぼ何もしない（本物との差 0.1〜0.3 dB）
- AUTO のハム探し: 完全に一定のビブラート（合成の歌 b_sing・rs_sing_04、5.4 Hz）の側帯波が 60 Hz の倍数に重なると、2 秒ほど誤って見つける（1.0 からある動き。test/humfalse.cpp）。
  人の声・ギターの揺れは一定でないので起きにくい。シンセの LFO ビブラートで起きうる（HUM は初期 OFF）
- HUM の WIDTH（Q）を急に動かすとノッチの係数が跳んでプチッと鳴った → 50 ms でならす（humLogQ）
- MOUTH の直す FIR を 1 タップごとに割り算で輪の位置を求めていて、192 kHz（1017 タップ）・全部 ON でブロック 128 の最悪 680 µs（持ち時間の 102 %）
  → 左右対称なのを使って輪の中身との内積 2 回（vDSP_dotpr）に。最悪 142 µs（21 %）。音は同じ（合わせ込みの差 4.1979 のまま）
- 1 回の処理時間（全部 ON、MacBook、test/blocktime_all）: 48 kHz はブロック 32〜128 で最悪 3〜8 %。192 kHz はブロック 64 で 41 %・128 で 21 %、
  ブロック 32 だけ STFT のフレームの回が最悪 73 %（50 % を超える回がある。1.0 と同じ限界）。CPU は 48 kHz 1.0 %・192 kHz 3.5 %（1 コア）

## 結果（自作 vs 本物、1/3 オクターブ × 10 ms ごとの下げ量の差の平均）
- HUM: インパルス応答の差 −80 dB（s_def・g20・b50・h4・h16）。AUTO はハム 60・50・揺れるハム（59.7 ± 0.25 Hz）・ジー音で約 1.5 秒後から本物の 60 Hz 固定と同じだけ取る
  （揺れるハムは本物の固定より多く取れる）。声だけ・響きだけ・雑音だけ（ファン風 120/240/360 Hz を含む以外）では見つけない
- NOISE: 54 通り（声＋雑音 4 種・持続音・段差・断続 × R 3/6/12/20・Threshold ±10、雑音だけ 4 種）で平均 0.93 dB。
  雑音だけの所の平均の下げ量は ±0.5 dB（R 20 の白色雑音だけ本物 −14.6 / 自作 −12.8）。LEARNED（nBiasL 11）は ADAPTIVE・本物と雑音の所 ±0.3 dB
- 持続音: 本物は 8 秒伸ばした母音を −7.1 dB まで削る。自作は −1.8 dB（上がる速さ 1.54 dB/秒で合わせ込みが落ち着いた。声全体の差を小さくする方を優先）。Fader Pilot で止めると −0.4 dB
- ROOM: 45 通り（声＋響き RT 0.6/1.2/2.5 秒 × R 5/10/15/20/−10・Tail 0.5/1/2/4、乾いた声、雑音の破裂 × 6 設定）で平均 2.84 dB（声だけ 2.60）。
  声の大きい所（上 40 dB）の平均: 初期設定 本物 −2.1 / 自作 −2.3、R 5 −0.2 / −0.2、R 15 −5.0 / −4.9、R 20 −8.1 / −6.2、Tail 0.5 −1.4 / −1.2、Tail 2 −2.7 / −3.4、Tail 4 −3.0 / −4.7、R −10 +4.4 / +5.8 前後。
  乾いた声に初期設定で −1.8 dB（本物 −1.0）。合わせ込みは fitroom.py（Nelder-Mead）、rBoostK だけ手で 1.41 → 1.1
- 数値テスト（build/noiselab/test/dsptest.cpp）: 44.1〜192 kHz × モノ/ステレオ × 極端な 8 信号 × 4 設定で NaN なし、全部 OFF の素通し −137 dB、切り替えのプチなし。
  1 回の処理時間は test/blocktime.cpp、ハムの誤検出は test/humfalse.cpp
- 画面の確認は build/noiselab/snap（画面に出さずに PNG へ書き出す JUCE のコンソールアプリ）

### DSP 検査係（1.1.0、2026-11-12）
- 判定「渡してよい」。遅れの申告とインパルスの位置が全サンプルレートで一致、DELTA の時間合わせ −151〜−157 dB、reset 後に古い音なし、ブロック長 1〜4096 でビット一致、
  極端な 192 通りで NaN なし、10 分連続でずれなし、つまみの急変 18 通りでプチなし、音のスレッドでメモリ確保・ロックなし。テストは scratchpad/t（t1c・t3_clicks・t6_loud・t2_cpu・t9_mouthfp）
- 直したほうがいい所として挙がり、直したもの:
  - HUM の WIDTH を下げると倍音の間の音楽が削れる（Q 30 で 440 Hz −54 dB、Q 100 で −20 dB）、Q 3000 は効ききらず鳴き残る → 範囲を 300〜2000（真ん中 1000）に。
    測定（test/qrange.cpp、60 Hz × 8 本・深さ 60）: Q 300 で 440 Hz −5.3 dB、Q 2000 でハム −59.6 dB・止めて 2 秒後 −70 dBFS（ハム −10.5 dBFS から）
  - MOUTH の感度 8〜10 で、ピチャ音のない歌・声の高域まで削った（感度 10 の歌の 2 kHz より上 −6 dB、本物 −0.01 dB）→ 感度の高い歌・声も目標に入れて合わせ直した。
    下限だけ（fitvoice3.py、しきい値 = max(14.3, …)）だと差 3.04 だが感度 3.4 より上でつまみが効かなくなるので、
    しきい値 = mThr0 − (mThr0 − mThrMin)·(感度/10)^mThrP（29.6 → 13.2 dB、p 0.48）にした（fitvoice4.py、差 3.31）。
    感度 10 の歌・声の変化は本物との差 0.07・0.23（前は 6 dB）。初期設定のピチャ音の取り方は少し離れた（m_clicks_quiet def 4.7 → 5.8）
  - PLOSIVE の画面の「pop −○ dB」が −121 dB まで出た（実際は全体で −80 dB 止まり）→ 表示も 80 で止める
  - Tuning の pBack（使っていない）を消した
- そのままにしたもの（README に書いた）: REDUCTION が浅い（30 dB 以下）と HUM の効き始めが数秒遅く、止めた後に鳴き残る（本物の Static も同じ式なので同じ。TailTime は 2 秒のまま）/
  1.0 で保存した自分のプリセット（.mypreset）は MUSIC で開く（PresetBar は「ない値は初期値」。Shared を変えると 20 個以上がビルドになるので見送り）/
  PLOSIVE は吹かれのない声でも 180 Hz 未満を約 −7 dB（本物 −7.3 dB）/ 1.0 の曲で GAIN MATCH を固定していると、ROOM の方式が変わった分の音量差は残る

- 再検査（2 回目、判定「渡してよい」）で挙がり、直したもの:
  - MOUTH の輪（mRing = mLa + 512 固定）が FIR の長さで決まっておらず、約 290 kHz より上で vDSP_dotpr が輪の外を読んだ（Logic・auval にないレート）
    → mRing = mLa + mFirLen + 512、FIR の半分 ≤ 先読みに制限。11.025〜384 kHz で読む位置が全部輪の中（scratchpad/r/t13_index）
  - 32 kHz は先読み 128 が「いちばん長いピチャ音 + 余白」148 より短く、長めのピチャ音の頭だけ直らなかった → ピチャ音の長さの上限を先読みに収まる長さまでに（直せない長さは触らない）
  - FREQ SKEW を +10 → −10 に一気に回すと、見る帯域が変わっても「まわりの大きさ」が前の帯域のままで、0.3 秒の間にふつうの声を 11 個ピチャ音とみなした
    → 動かした量に応じて見分けを止める（±10 → ∓10 で 0.1 秒、mHold）。0 個に、t3 の跳ね +6.3 → −0.1 dB
  - Tuning の hQ（使っていない）を消した。README の「Q 300 で最大 −6 dB」は倍音の本数で変わる（8 本 −5、12 本 −9、16 本 −13 dB）ので書き直した
  - 48 kHz の音は変わらない（口の合わせ込みの差 3.3069 のまま）
- ROOM をマイナス（響きを足す）・帯域 4 本 10・OUTPUT +12 に +22.5 dBFS の雑音を入れると出口 +43 dBFS（t6_loud）。上げ量の上限は rBoostK × 10 = 13.4 dB ＋ OUTPUT 12 dB で決まっていて設計どおり（係も同じ判断）

### 1.1.0 の結果（同じ物差し: 1/3 オクターブ × 10 ms ごとの下げ量の差の平均、+ 0.5 × 平均のずれ）
- ROOM（Habets 型に変えて合わせ直し、fitroom6）: 45 通りで 2.26（1.0 の方式 2.84 → 新方式 1 回目 2.38）、合わせ込みに使っていない確認用 2.07。
  帯域 4 本（fitroomband2）: 55 通りで 1.50（帯域を合わせる前 2.68）、確認用 1.86。境目 596 Hz・1.49 kHz・4.0 kHz、なだらかさ 0.24 オクターブ、効き方 (s/6)^5.47（本物も 6 から少し動かすだけでよく効く）
- 口（fitvoice4、感度の高い歌・声も入れた 21 通り）: 3.31。きれいな声・歌には感度 10 でもほぼ何もしない（0.0〜0.2）。感度 2・SKEW +5〜+10 で本物と大きく違う（上の「限界」）
- 息: 12 通りで 1.36（初期値 3.00）。声の所は本物と同じく触らない（0.00）。REDUCTION 40〜100 で本物は −26 dB 前後で止まる → bSat で同じ
- 吹かれ: 14 通りで 2.90（初期値 9.71）。STRENGTH 2 だけ本物より浅い（差 10 dB）、FREQ LIMIT 400 Hz で差 8 dB
- NOISE の MUSIC / GENTLE（fitnoise7.py）: 雑音だけの所が多いと「伸ばした音を削りすぎる」方向に合ってしまった（2 回目の MUSIC は 8 秒の母音を本物 −1.6 dB に対して −3.5 dB）。
  3 回目は「鳴っている所（帯ごとの下から 10 % の大きさ + 10 dB より上）」と「雑音だけの所」を半々の重みにし、伸ばした母音の 1 秒ごとの削られ方も目標に足した。
  8 秒の母音の 1・3・5・7 秒後: MUSIC 本物 −0.7/−1.0/−1.3/−1.6・自作 −0.7/−1.0/−1.2/−1.6、MUSIC+GENTLE 本物 −1.3/−1.7/−2.0/−2.3・自作 −1.5/−1.8/−2.2/−2.5。
  DIALOGUE（と DIALOGUE+GENTLE）は本物が −7.1（−8.4）dB まで削る癖があり、まねない（自作 −1.8・−2.8。目標から外した）。
  前の物差し（全部の所の平均）では MUSIC 1.28・GENTLE 1.43（初期値 1.71・1.44）。雑音だけの所の一致を少し譲って、声・伸ばした音を合わせた
- 本体に書いた値で同じ数になるかを verifyroom.py・verifyvoice.py・verifynoise.py で確かめた
