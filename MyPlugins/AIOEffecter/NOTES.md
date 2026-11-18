# AIO Effecter — 作業メモ（Claude 用）

aufx Aifx Mypl。2026-11-18 本人「guitar rig 7 pro … いろいろ実験してみて音の響きとか抑制とか、そのまんま同じものを作ってほしい。名前は aio effecter」
＋「将来的に AIO Guitar のエフェクター欄にこれを移植したい」。

## 決まったこと
- mix-engineer は「形を変えれば価値あり（AIO Guitar のリグを弾いたギターに挿せる形＋DI で判定するゲートだけ。リバーブ等は入れない）」。
  本人は形の質問に答えず、Guitar Rig 7 Pro を Native Access で有効にして（21:37〜21:41、lic が "pro" に）全部測れるようにした → Guitar Rig と同じく部品を並べるラックで、版を重ねて部品を足す形にした。
  0.1.0 は本人の言葉の「響き」「抑制」から 7 部品
- 音の処理は fx/ の JUCE なしのヘッダー（Module.h の形）。AIO Guitar へ移すときは CMakeLists で ../AIOEffecter/Source を include して fx/Registry.h・Rack.h を使う
  （build.yml の「一緒にビルド」は ../Shared だけ見ている。AIO Guitar が include したら、AIO Effecter を変えたとき AIO Guitar も手でビルドする）
- 見た目・名前はオリジナル（NOISE GATE / NOISE REDUCTION / TUBE COMP / LIMITER / ROOM VERB / SPRING / DUAL DELAY）。つまみは Guitar Rig と同じ数・同じ 0..1 の尺度（測った式で実際の値に直す）

## 仕組み
- fx/Module.h … 部品の共通の形（prepare / reset / setKnob(0..1) / process(L, R) / meter）、ModuleInfo（id・名前・つまみの名前・初期値・値の文字）
- fx/Registry.h … 部品の一覧。**番号（type）は保存に使うので並びを変えない。足すときは後ろへ**（0 = 空き）
- fx/Rack.h … 8 段。type が変わったらメッセージスレッド（プロセッサーのタイマー 20 Hz）で作って prepare → incoming に置き、
  オーディオスレッドが 15 ms でドライへ寄せてから入れ替え、古い部品は outgoing → タイマーで消す（オーディオスレッドで new / delete しない）。
  ON / OFF は 15 ms のクロスフェード、OFF の段は計算しない（戻すときに reset）。部品の出力が NaN / inf ならドライに置き換え
- パラメーター: s1_type〜s8_type（Int 0..255 固定、部品を足しても意味が変わらない）、s?_on、s?_k1〜k12（0..1）。gainMatch、linkGroup、linkMatch
- 画面: 8 段（^ v・ON・部品の選択・メーター・つまみ最大 12）、RACKS（内蔵ラック 5 個）、下に LINK 欄＋GAIN MATCH・プリセット・UNDO（2 段）
- Plugin Link: Kind 28（PluginLink.h には未登録）。Gain Stage の LEVEL MATCH を受けるだけ（MATCH ボタン）
- 遅れ 0。テール 30 s

## 測り方（build/aiofx、コミットしない）
- 測定ホスト build/grlab/gh（2026-11-18: 落ちたときに macOS の「予期しない理由で終了」を出さないよう SIGSEGV 等で _exit(70) にした）
- **Guitar Rig を同時に何個も動かすと、ときどき NaN が 1 ブロックに 1 サンプル混じる**（6 並列で 2〜3 割）。NaN でないサンプルは 1 個ずつ動かしたときとビット単位で同じ
  → 並列で測って NaN のものだけ 1 個ずつやり直す（run.sh・caprun.sh・finite.py）
- Split B / Split Mix / CrossOver B / CrossOver Mix は単独で読ませると Guitar Rig が落ちる（相方が要る部品）→ 測らない
- 全部の記録: cap/<dyn|verb|amp|mod>/<部品>/<設定>.npz（float16、L = R なら 1 本）、140 部品 7,037 通り。capgen.py → caprun.sh。
  試験の音 dynS / verbS / ampS（= grlab/ped/test.f32）/ modS（.json に区切り）
- 合わせ込み: tools/render.cpp（部品 1 個に音を通す CLI。k? = つまみ、F.? = 合わせ込みの係数）を build/aiofx/render にして、cmp.py / verbcmp.py で本物と比べる
- 試用・デモについて: Guitar Rig 7 の無料版から「デモ」に切り替えると、外からラックを読ませる ClassInfo が kAudioUnitErr_Unauthorized（−10847）、
  ClassInfoFromDocument は未対応（−10879）。読み出し（GET）はできる。build/grlab/grlive（画面つきの測定ホスト）と build/aiofx/mega（測定用ラック .ngrr）はそのときの道具（Pro を有効にしたので使わなかった）

## 部品ごとの合い具合（0.1.0）
- NOISE GATE（gate）: 判定は山（L/R の大きいほう）、開く = 閉じる大きさ。THRESHOLD −63 + 60v dB、HOLD 0.1 × 300^v s、ATTACK 直線 0.1 ms × 10000^v、RELEASE 指数 1 ms × 10000^v。
  **波形の差 −64〜−92 dB（ほぼ同じ）**（gate/gateT.f32 の 9 通り）
- LIMITER（limit）: OUTPUT −20 + 40v dB → 天井 −40 + 80v dB、一瞬で抑える、HOLD 0.1 × 300^v、RELEASE 86.9 × 100^−v dB/s。
  段差のテスト 5 通りで音量の動きの差 平均 0.06 dB（波形 −40 dB）。強く抑え続ける階段（Limit 0 など）では最大 3 dB の差が残る（要調べ）
- DUAL DELAY（twindly）: TIME 1 + 1999v² ms（輪の中に 1 サンプル余分）、PRE 2000v ms、LEVEL・FDBK = v、CROSS = v（0.5 まで）、WIDTH、MIX（真ん中で両方最大、15/16 が掛かる）。
  反射の時刻 ±0.02 ms・大きさは合う。端数の補間は本物が両側に波打つ（窓付き sinc ふう）、こちらは 4 点 → インパルスで高域に差（ギターの音域では小さい）。
  本物は右の入口を使わない（こちらは L+R の平均）。SYNC（テンポ同期）は 0.1.0 では無し
- NOISE REDUCTION（nred）: 1:4 のエキスパンダー g = 1/(1+(thr/env)^3)、THRESHOLD −85.75 + 90v dB、env は山・下がる時定数 26 ms × 16^(v−0.5)。
  階段の差 0.3〜1 dB。DEHISS は高い音（2 次ハイパスの側鎖）で 1 次ローパス（1 kHz〜）を開く形 → 係数は nrfit で合わせ込み
- TUBE COMP（tubecomp）: SATURATION（押し込み＋やわらかい飽和 x/(1+|x/c|^p)^(1/p)）→ INPUT → コンプ → OUTPUT（INPUT を上げても歪みが増えない本物に合わせた順番）。
  係数は tcfit（階段の各段の大きさ・3 次の歪み、つまみ 9 本 × 5 段 = 43 通り）で合わせ込み。ATTACK 0.5 ms × e^(8a)・RELEASE 3 ms × e^(13.5r) は本物の段差から（初期値で戻り約 80 ms、0.5 で数秒、0.75 から上は止まる）。
  **本物の立ち上がりは独特**: 静かな所から大きい音に変わった瞬間に一度深く下がり、そこから上がって行き過ぎてから落ち着く（ATTACK 0.5 で 0 dB → +5.2 → +4.4 dB）。ATTACK 1 は 900 ms 動かない。
  こちらは普通のコンプの立ち上がり（大きい音の頭が数 ms 大きく出る）→ 0.2 以降で合わせる。
  **RATIO 0.5 から上の差 3〜7 dB**: 本物は −60 dB の小さい音から、なだらかに強く圧縮していく（RATIO 1 で −60 → −30 dB の傾き 0.45、−30 → 0 dB で 0.09）。
  こちらはしきい値の近くで曲がるソフトニー → 0.2 以降でコンプの曲線の作りを変える（対数っぽい形）。最初に静的な形だけで合わせたら戻りが 0.5 ms（波の中で動いて小さい音の歪みを作る）になったので、時間は固定して歪みは飽和の形で作った
- ROOM VERB（studiovb）: 8 本の FDN＋入口のオールパス 4 段＋初期反射 12 本。T60 の表、TREBLE = 2 次ローパス 200 × 81^v Hz、BRIGHT = 4 kHz の T60 比の表、
  WET（ドライ 15/16 (1 − w²)、ウェットは表）、PRE-DELAY 186v ms。響きの量 ±0.6 dB、T60 ±5 %、音色 ±1 dB、最初の響き +1.8 ms
- SPRING（spring）: オールパス 24 段の分散＋遅れ線の輪 × 左右（長さ 1 : 1.07）、LENGTH 55v ms、SIZE の T60 の表（輪のフィルターの最大で正規化）、
  LOW DAMP = 輪の 400 Hz の棚＋出口の 250 Hz の棚、HIGH DAMP = 輪の 1 次ローパス＋出口の 2.5 kHz の棚、BASS = 出口のハイパスの角と 3 kHz の棚、量の補正の表。
  **いちばん大まか**: 量 ±1.5 dB、1 kHz の T60 は合う、4 kHz の T60 は長い所で 2 倍、ばねの「ビヨン」の形そのものは本物と違う

## DSP 検査（2026-11-18）
- 1 回目「危ない」: SPRING が LOW DAMP ≤ 0.2・SIZE ≥ 0.75・LENGTH ≤ 0.04 で直流〜40 Hz が増え続けた（輪の棚の持ち上げを 100 Hz より上でしか見ていなかった）
  → 輪の中に直流止め 25 Hz、正規化は 0〜12 kHz の 30 点の最大 × 1.002、輪の中の頭打ち、LENGTH は 2 ms 以上。
  最初は「下限 1」も入れたら長い SIZE の響きが半分になった（輪のフィルターが全体を下げている分まで割っていた）→ 外した
- ほかに直したこと: 部品の入れ替え待ちの間は古い部品に新しいつまみを入れずドライへ、ON 直後は入口も 15 ms フェードイン、
  NaN / inf / |x| ≥ 1000 が出たら部品を reset（入口の NaN は 0 に）、なめらかでないつまみ（SPRING 全部・TUBE COMP の THRESHOLD / RATIO / KNEE・NR の THRESHOLD・ROOM VERB の SIZE / BRIGHT / TREBLE・LIMITER の LIMIT）、
  DUAL DELAY の頭打ちを 1.0 に、type 8〜255 は空き・オートメーション不可、最後が LIMITER なら limitBoost (0)、reset() で全段 reset、TUBE COMP の RELEASE は最長 60 s、出口 ±16 で頭打ち
- 2 回目「直してから」: DUAL DELAY で FDBK と CROSS の合計が 1 を超えると増え続けた（本物も同じ作り）→ 輪の 2×2 行列の最大固有値 λ が 1 を超えたときだけ 1/λ 倍に縮める
  （本物と比べた CROSS 0 / 0.25 / 0.5 の数字は変わらない）。NR の ldHF をなめらかにしたしきい値から（プチッ 156 → 7）、DEHISS のローパスはいつも回す、
  TUBE COMP の曲線と LIMITER の天井を毎サンプル、SPRING の輪の頭打ちは 4 から（0.8 だと 0 dBFS のギターで響きがつぶれた）、部品の選び直しはその場で service
- **持ち越し**: TUBE COMP の SATURATION に 4 倍オーバーサンプリングがない（決まり「歪み系は 4 倍」）。7 kHz・−6 dBFS で折り返し −33.6 dB（SAT 0.59）〜 −14.5 dB（SAT 1）。
  0.2.0 でアンプ・歪みと一緒に、共通の 4 倍（遅れの申告をラックでそろえる）を作って入れる
- 検査係のテスト一式は build/aiofx/test（run_all.sh、test_spring.cpp など。コミットしない）

## 次に
- 部品を足す（全部の記録が cap/ にある）: アンプ（Fire Seeker・Jump・ICM の新しいアンプ）とキャビ、歪み、揺れもの、フィルター、ピッチ。足すたびに Registry の後ろへ
- AIO Guitar のエフェクター欄へ移す（本人の将来の希望）
- DUAL DELAY の SYNC、LIMITER の強い抑え続けの差、SPRING の形
