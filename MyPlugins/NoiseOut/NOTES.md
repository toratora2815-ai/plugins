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
- Music・Gentle は合わせていない（初期設定との差 1.3 dB 程度）。Spectral De-noise とは帯域ごとの動きがかなり違う（平均 4〜5 dB）ので別物

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
