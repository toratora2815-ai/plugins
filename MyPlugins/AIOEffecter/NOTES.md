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

## 0.2.0（2026-11-19）抑える系の残り
- 本人「一旦、大まかにエフェクター作っていって、いらんくなった測定結果を消して、さらに細かい測定」→ まず全部を大まかに作る。0.2.0 は抑える系の残り 12 部品（Gater は保留）
- **測り直し**: Guitar Rig はでたらめに（1 割前後）部品を間違った値で組み立てる（VC 76 が素通し、VC 2A が固まるなど。DUMP の読み戻しは正しいので気づけない）。
  2026-11-18 の記録（並列 1 回）は信用できない → 13 部品 509 件を「状態をエンジンの準備より前に読ませる」gh4 で、2 回測ってビット単位で同じものだけ採用（capone2.sh）。14 件は 3 回目で一致。
  追加で測ったもの: Supercharger GT のモード違い（SATURATION 0.5・CHARACTER 0.5 で）、Maximizer の本当の範囲（THRESHOLD / CEILING は dB、−20〜0）、
  Volume Pedal の MIN / HALF、Solid Dynamics のゲート（RANGE 1 で）、Gater（テンポ 120、gh5）
- **共通エンジン（fx/DynEngine.h）＋表（fx/DynTables.h、自動生成）**: エンジンの値（PRE・THR・RATIO・KNEE・ATT・REL・MAKEUP・DRY・WET・SAT1/2・SCHP・RMS・棚・ゲート・先読み・OUT）を、
  つまみ 1 本ずつの記録に合わせ込んで表にする。つまみの効き目は「初期値の点との差」を足し合わせる（つまみ同士の組み合わせは測っていない = 大まか）。
  合わせ込みは build/aiofx/fit（fitkit.py = 共有ライブラリを ctypes で、dynfit.py = 静的な曲線の多点スタート＋Powell、gentable.py → DynTables.h、部品ごとの *.py）。
  目標 = 31 段の階段の大きさ・3 種類の段差の動き（1 ms ごと、最初の 1 ms は除く）・2 次 / 3 次の歪み・ピンクノイズの帯域・同相の利得（極性）
- 飽和の段がある部品は 4 倍（fx/Oversampler.h、IIR のハーフバンド 2 段、遅れ約 4 サンプル・申告しない）をいつも通し、**ドライも同じフィルターを通して位相をそろえる**（MIX の途中でずれていた）
- つまみを回してもプチッとしないよう、しきい値・ニー・傾き・ゲートのしきい値と深さも 20 ms でならす
- 先読み（MAXIMIZER 429 サンプル = 先読み 353 ＋ 4 倍 4 ＋ 天井を守る 72）: Rack が段ごとにドライも遅らせ（OFF の間も）、入れ替えで遅れが変わるときは 15 ms でつなぎ替え。合計をプロセッサーがホストに申告
- **DSP 検査 1 回目（0.2.0）「直してから」** → 直したこと:
  表 1 本の値の数 6 → 10（VC 76 の RATIO が 8 個で配列の外に書いて落ちた。gentable.py で数を確かめる）/ TRANSIENT の CLIP はいつも 4 倍を通して効き具合を 20 ms でならす（ON/OFF のプチッ 818 → 1）/
  MAXIMIZER は窓 [今 − 353, 今] の最大 → 353 サンプルの箱形の平均（音が届く前に下げ終わる。最後の頭打ちに頼らない）、荒さ（CHARACTER 2.5 未満）は天井を最大 +6 dB 上げて 4 倍のやわらかいクリップで削る（折り返し −22 → −72〜−92 dB）/
  新しいつまみ k13〜16 の版のヒント 11 / 飽和の偏りは |d·b| ≤ 1.5（片側が何倍にもならない）、tanh(d·b) は元の 1 サンプルに 1 回（CPU 約半分）/
  先読みのバッファーをステレオ分・ドライは 4 倍を通したものを遅らせる / 棚は動いている間だけ毎サンプル計算（ジッパー）/ 2 段の戻りはいつも両方回して割合をならす / ゲートが OFF になってもなめらかに開く /
  4 倍の遅れ（1 段約 4 サンプル）を latency() で申告 / ラック: 遅れのある部品は OFF の間も回す（ON にした瞬間に先読みが空で抜けない）、reset でドライの遅れの中身も消す
- **DSP 検査 2 回目「直してから」（MAXIMIZER の天井だけ）** → 荒さありで 4 倍から戻した波形が天井を最大 +8 dB 超えていた・荒さなしでも使わない 4 倍フィルターの位相で clamp にかかっていた。
  荒さなしの道を 4 サンプルの整数の遅れにし（荒さありの 4 倍クリップと 20 ms で入れ替え）、最後に 1.5 ms の短い先読み（同じ箱形）で天井 1.0 を守る。clamp を外しても天井ちょうど 1.0000、遅れ 429（48 kHz）。
  ほか: 響きのない部品は ON のとき入口をフェードインしない（−2.3 dB の谷）、入口・MAKEUP・出口の音量のならし 60 ms。
  **3 回目「直してから」**: 窓の最大を覚える列が 1 つ短かった（W = look + 3 に。34 Hz より下で山を見誤り低音の歪みが 10 dB 増えていた）、
  再生中に MAXIMIZER を選ぶと空の先読みからいきなり音が出てプチッ（目安 775）→ 動かし始めた部品は先読みが埋まるまで（lat サンプル）ドライのまま（Rack の prime）
  **4 回目「直してから」**: 入口・MAKEUP・出口を倍率でならしていたので、逆向きに動く 2 つ（DIRT の ON/OFF、INPUT と OUT）が途中でふくらんだ（TUBE PUSH の INPUT 0→1 で +20.9 dBFS が 0.1 秒）
  → dB でならして使うときに倍率へ（ふくらみ 1 dB 超え 38 → 3 / 840 通り）。prepare で prime・running を 0 に、ドライの遅れのつなぎ替えが終わるまで次の入れ替えを待たせる。
  検査係の道具: tools/test_inspect.cpp（ti）・tools/test_jump.cpp（tj）
  **5 回目「渡してよい」**。持ち越し（任意）: dB でならしたので、音量のつまみを「いちばん下」（−98〜−124 dB）へ一気に飛ばすと下がり始めに角（2 kHz より上へ −50.7 dB、プチッの目安 4.6〜6.8）。
  2 段ならし（9 ms＋51 ms、Smooth2）で消えるが PEDAL COMP SUSTAIN 0→1 の山が +0.5 dB 悪くなる（検査係の写し scratchpad/r5/casc2）2 回目に見つかった 0.1.0 からの TUBE COMP の全開（+100 dBFS 近く → ラックの頭打ち）は持ち越し
- 合わせ込みで分かったこと: TUBE PUSH / XL の INPUT は「コンプへの入口を上げて出口で戻す」（PRE と OUT。最初はしきい値で合わせたら INPUT × COMPRESS の組み合わせで最大 9 dB ずれた。組み合わせ 5 通りを測って確かめた）/
  Guitar Rig の Supercharger GT は自分でも 8 サンプル遅れる → 比べるときは本物もずらす（dynfit.GRLAT）/ Solid Dynamics のゲートは「しきい値を下回っても HOLD の間は開いたまま」→ dpGHold を足した（開くときの待ち方はまだ合っていない）
- kMaxKnobs 12 → 16（Solid Dynamics が 14 本）。0.1.0 はビルドされていない（GitHub の Mac が動かなかった）ので、パラメーターの数を変えても困る人はいない

### 部品ごとの合い具合（0.2.0、build/aiofx/fit/verify.py = 出来上がった部品を本物のつまみ 1 本ずつの記録と比べた中央値）
- TUBE PUSH（Supercharger）静的 0.16 dB・動き 0.24 dB（DIRT ON 1.6 dB）。INPUT × COMPRESS の組み合わせは中くらい 0.3〜0.8 dB、端（INPUT 0 / 1 と COMPRESS 0 / 1）2〜5 dB
- TUBE PUSH XL（Supercharger GT）0.15 / 0.13 dB。SAT MODE・CHAR MODE は SATURATION 0.5・CHARACTER 0.5 で測って、モード 2 との差を足す（gtmodes.py）
- FET COMP（VC 76）0.31 / 0.47 dB。DRY は「元の音を足す量」（100 % = 並列コンプ。下げると大きい音ほど小さくなる、本物と同じ）
- OPTO COMP（VC 2A）0.31 / 1.2 dB（2 段の戻りがまだ大まか。GAIN 1 は真空管の歪みで 3 dB）
- VCA COMP（VC 160）0.11 / 0.18 dB
- BUS COMP（Solid Buscomp）0.84 / 1.0 dB（ATTACK の速い段で 2.3 dB。本物は ATTACK で静的な曲線も変わる）
- CHANNEL DYN（Solid Dynamics）0.48 / 0.29 dB。ゲートは RANGE 1 で測った別の表（sdgate.py）、静的 0.7 dB・開くときの動きはまだ 7 dB ずれる
- QUICK COMP（Fast Comp）0.41 / 0.91 dB、PEDAL COMP（Stomp Compressor）0.21 / 0.31 dB（RELEASE SLOW 3.6 dB）
- TRANSIENT（Transient Master）静的 0.2〜1.1 dB（tmfit.py で 21 通りをまとめて）
- VOLUME PEDAL: 本物と 0.01 dB で同じ（放物線）。MAXIMIZER: 静的 0.0〜0.15 dB、CHARACTER 0 の 3 次の歪み −13.8 dB（本物 −14）、1 は −15.9 dB（本物 −30、こちらが荒い）
- 保留: Gater（テンポ 120 で測ったが RATE が効かない。刻みは 1 拍ごと）→ 細かい測定のときに
- CPU: 1 段 1〜2 %、8 段全部 FET COMP で 16.5 %（48 kHz）。重いのは 4 倍のフィルター（ドライも通す）と tanh

## 0.3.0（2026-11-19）響き系
- **測り直し（capone3.sh）**: 響き・揺れ・アンプの全 6,424 件を、1 件ずつ「2 回測ってビット単位で同じ」で測り直し中（2 つ同時、run_all3.sh）。
  1 回目が 2026-11-18 の記録と同じなら 1 回で終える（同じだった古い記録は cap_old から消す。違ったものは残す）。
  **毎回少し違う音を出す部品がある**（Raum の MODE 3〜11、Replika GR の一部。ランダムな揺れ）→ 3 回測って、1 秒ごと × オクターブ 8 帯域のエネルギーが近い 2 つ
  （差の 2 乗平均 0.6 dB 未満・95% 点 1.5 dB 未満）の真ん中を残す（featcheck.py、cap/nondet3.txt）。
  初期値で効かない作りのつまみ・足りない値は追加で測る（cap/list6〜8.tsv、run_extra3.sh。Little Reflektor の IR 8 個、Traktor Delay は初期値が MUTE ON だった、Ice Verb の SIZE / MIX / STEREO の途中）
- **共通エンジン（fx/VerbEngine.h）＋表（fx/VerbTables.h、自動生成）**: 予備の遅れ（くり返し）→ こだま／初期反射 16 本／拡散 4 段 → 8 本の FDN（低・中・高の 3 帯域で消え方、揺れ）、
  響きだけの遅れ（vdel）、音色（ローカット・ハイカット・3 帯域・4 次の帯域制限）、広がり、FREEZE、逆回し（96 本のだんだん大きくなるタップ）。遅れなし。CPU 1 個 0.4 %（逆回し 1.2 %）
- 合わせ込み（build/aiofx/vfit）: vkit.py（ctypes）・verbfit.py（部品ごとの設定、つまみ 1 本ずつ）・gentable_verb.py・verify_verb.py（出来上がった部品と本物の比較＋ギターの DI での帯域）。
  特徴は C++ の同じ式で本物とエンジンの両方を測る（fitlib.cpp の aiofx_verb_features）: 響きの始まり（2 ms ごとのエネルギーが最大の −20 dB）・量・最初の 50 ms の割合・左右の相関・
  T60（125 Hz〜8 kHz）・音色（63 Hz〜16 kHz）。ドライは「ドライだけの記録」を形にして最小二乗で抜く（本物はドライにも小さなフィルターがかかっている）。
  初期値の合わせ込みは 6 つの出発点から並行（遠い所で止まるのを避ける）→ 初期値と同じ設定の記録（MODE の初期の番号など）の合わせ込みのほうが良ければ土台を取り替えてもう一度
- MIX の法則は部品ごとに測って式で書いた（表にしない）: Raum = ドライ min(1, 2(1−m))・ウェット min(1, 2m)^1.5 / Vintage Verb・Little Reflektor・Traktor Reverb・Ice Verb・Octaverb = ^1 /
  RC 24 = cos・sin（エネルギー）/ RC 48 = 測った表 / Reflektor = WET・DRY が別々の直線。ドライの土台は「ドライだけのときの大きさ」（初期値のドライで割らない）
- 部品ごとの決まり（adjust）はコンストラクターでは呼ばれない（C++ の仮想関数）→ prepare の頭で必ず計算し直す（初期値のつまみのとき MIX の法則が効いていなかった）
- 測り方の工夫: 響きの始まりは「2 ms ごとのエネルギーが最大の −20 dB」（RC 24 のドライの通り道のゆっくり減る小さな尾に引っぱられないように）/
  音がほとんどない帯域（量 −30 dB 以下）の T60 は軽く見る（Ice Verb の狭い帯域）/ 予備の遅れが初期値より短い所は響きだけの遅れ（vdel）を縮める /
  合わせ込めなかった点（RC 48 の MID 1 は本物の響きがほぼ消える）は表に入れない

- **DSP 検査 1 回目（0.3.0）「危ない」** → 直したこと:
  1. DIGI HALL の発振（BASS 70 % 以上・MID 40〜70 % で 1 周の大きさ最大 1.042、ラックで +24 dBFS に 12 秒張り付く）: 低い棚と高い棚の持ち上げが 0.9〜2 kHz で重なっていた。
     遅れ線ごとに、消え方のフィルターの大きさを 26 点（0 Hz・20 Hz〜0.49 sr・ナイキスト）で調べて、どの周波数でも 1 周 ≤ 0.9995 に押さえる（FREEZE は 0.99995）。BASS 100 %・MID 50 % で毎秒 +3 dB → −1 dB
  2. つまみのプチッ: 遅れ線の長さ・初期反射のタップ・拡散の長さ・拡散の係数・消え方の係数（大きさと 1 次の棚）・響きの量の係数・揺れの深さを 1 サンプルずつつなぐ（32 サンプルの階段にしない）/
     音色のフィルターを TPT の状態変数フィルター（Svf）に替え、g・k・m を 1 サンプルずつつなぐ（止まっている間は割り算しない）/ ローカット・ハイカット・帯域の上限はいつも動かして、効き具合を元のしきい値の前後だけで混ぜる /
     逆回しのタップは 2 組持って、作り直したら 30 ms で入れ替える / 選ぶつまみ（MODE・PRESET・ROOM・ALGORITHM）を変えたら、ウェットを 15 ms で下げる間は古い値のまま →
     底で響きの中身を空にして切り替え → 40 ms で戻す（空にした中身へ入る音もゆっくり戻す。響きだけの遅れ 0 の部屋で戻し忘れていてプチッが残っていた）
  3. SPACE VERB の FEEDBACK が PREDELAY 0 で 1 サンプルの輪になり低音 +26 dB → 予備の遅れ 10 ms 未満ではくり返しを弱める（本物も PREDELAY 0 では効かない）/
     予備の遅れが変わったら 40 ms のクロスフェード（大きく飛ぶと音程が鳴き上がっていた）/ こだま（REVERB 0 %）に DECAY などの響きの量の補正が掛かって +13.4 dB → HIGH CUT・LOW CUT の分だけ残す
  4. GAIN MATCH の「センド使い」の判定に新しい響きの MIX / DRY のつまみを足した（PluginProcessor の mixRules。FROST VERB は初期値が MIX 100 %）
  検査係の道具: tools/test_verb.cpp（stab・click all wet・rev・fbpk など）。直したあと: 発振 0、プチッの目安 10 以上 0 個、本物との差は前と同じ（DIGI HALL XL 0.38 など）。CPU 1 個 0.36 → 0.66 %
- **DSP 検査 2 回目（0.3.0）「直してから」** → 直したこと:
  1. 1 周 ≤ 1 の押さえが 26 点の間の山を見逃し、DIGI HALL の特定の組で 96 kHz は減らず・192 kHz は毎秒 +0.22 dB 増え続けた →
     2 つの 1 次の棚の積の大きさの 2 乗は cos ω の「2 次 ÷ 2 次」なので、最大をちょうど計算する（maxMag2Pair: 両端と微分 = 0 の根）。どのレートでも最大 0.9995
  2. つなぎの途中で 1.0001 まで: 大きさを上げるときは、つなぐ前の棚の最大でも押さえる / 消え方の棚を TPT の 1 次ローパスの形 y = gHF·x + (gDC − gHF)·LP(x) に替えた
     （同じ伝達関数。前の形は角が低いと 0 Hz の近くで係数の丸め誤差が 60〜250 倍になり、FREEZE 中に 1.00001 を超えた）/ つなぎは最後にちょうど目標に着ける
  3. 直流がたまる（DIGI HALL 全部最大で 620 倍、XL 初期値で 7 倍。直流は全部の遅れ線で必ず共振する）→ 網・こだま・逆回しに入る音だけ 2 次のバターワース 10 Hz で直流を切る
     （63 Hz で −0.003 dB。ドライは触らない。状態は reset で 0、切り替えのダックでは消さない）
  4. FREEZE 中に MODE などを変えると、中身を空にしたあと入口が閉じたままで止めていた音が消えた → FREEZE を外すまで切り替えを待つ（外したときに 1 回のダック）
  5. 選ぶつまみを速く切り替え続けると、入る音の戻りが飛んでプチッ（PICK ROOM 20 ms ごとで 16.8）→ 戻りはダックの状態と別の「上がるだけ」の値に（1.0〜2.5）
  直したあと: 1 周の最大（中の係数から周波数 2000 点）0.9995・つなぎの途中 0.99995、192 kHz の例は 300 秒で −2.5 dB / 10 秒ずつ減る、
  直流 0.00 倍、本物との特徴の差は前と同じ（T60 ±0.01、音色・量 ±0.07 dB）。検査係の新しい道具: tools/test_verb_wb.cpp（中の係数を直接読む。static・refine・long・ramp・duck・dc・hold）
- **DSP 検査 3 回目（0.3.0）「渡してよい」**。おすすめの直し 1 つだけ入れた: ラックはつまみを番号の小さい順に入れるので、プリセット・UNDO で「選ぶつまみ → FREEZE ON」が
  同じブロックに来ると、FREEZE が入る前に切り替えが始まって止めた音が無音になっていた → 選ぶつまみの段の変化は setKnob では待ち（stepPending）、process の頭で
  そのブロックの FREEZE を見てから applyPending（検査係が試したのと同じ形）。「切り替えて 10〜30 ms 後に FREEZE」はまだ無音になる（人の手ではまず起きないので持ち越し）。
  CLUB VERB の SIZE を一気に動かしたときの C 16〜18 は、動かす瞬間のずれで 4.5〜31 にばらつく測り方の揺れ（直す前も同じ）。Shelf1（TPT）は前の形と大きさの差 2.9e-6 以下。
  3 回目の道具: tools/test_verb_wb3.cpp（shelf・exactramp・freezestep・clubc）

### 部品ごとの合い具合（0.3.0、vfit/verify_verb.py = つまみ 1 本ずつの記録と比べた特徴の差の中央値 / ギターの DI での帯域ごとの音量差の中央値）
- SPACE VERB（Raum）0.37 / 0.1 dB。MODE 2（SWELL）は 1.0（ゆっくりふくらむ形がまだ大まか）
- CLASSIC VERB（Vintage Verb）0.19 / 0.1 dB。MODE 5・6 は 1 前後
- SHAPE ROOM（Reflektor）0.26 / 0.1 dB。START 0.75（とても暗い）2.0、PEAK GAIN 1 は 2.8。REVERSE は 96 本のタップ（本物の逆回しの形と 2〜4 dB）
- PICK ROOM（Little Reflektor）0.27 / 0.2 dB（IR 8 個とも 0.4 以下）
- CLUB VERB（Traktor Reverb）0.61 / 0.6 dB（SIZE 0.75 以上で長さが 2 割短い）
- DIGI HALL（RC 24）0.26 / 0.1 dB。BASS・MID を上げきった所で 1.3〜2.4（本物は 8〜12 秒まで伸びる）
- DIGI HALL XL（RC 48）0.38 / 0.2 dB
- FROST VERB（Ice Verb）0.77 / 0.8 dB（狭い帯域の響き。ICE 1 は 3.2）
- EIGHT VERB（Octaverb）0.23 / 0.2 dB
- 合わせ込みの値の意味: 特徴の差 = T60（log、1.5 倍）・音色（dB、0.7 倍）・量（dB）・最初の 50 ms の割合・始まり（ms × 0.3）・左右の相関（× 5）の 2 乗平均

## 0.4.0（2026-11-19）ディレイ
- 部品 29〜34（fx/DelayParts.h、共通の部品は fx/DelayKit.h）。測り方: ノイズの塊のこだま（帯域ごとの利得・くり返しの大きさ、左右の力を平均）と、
  インパルスの返り（こだまの時刻・左右の分かれ方）。比べる道具は build/aiofx/vfit/dcmp.py・dburst.py、作業用は scratchpad の tecmp.py など（コミットしない）
  - WARP DELAY = Psyche Delay: TIME = 5.02 × 10^(2.6 v) ms、WET はエネルギー、CROSS でピンポン、REVERSE・PITCH・DETUNE は大まか
  - BBD ECHO = Delay Man: 6 次のバターワースのこだま、コンパンダーの癖（小さい音ほど速く消える）、FEEDBACK 1.26 × v^2.4 で発振、ACCEL で時間を追いかける（音程がすべる）
  - WIDE DELAY = Quad Delay: 1 次の LP / HP が輪の中、MOD DEPTH で左右別々に揺れる、DIFFUSION は長いオールパス 4 段
  - CLUB DELAY = Traktor Delay: 7 段の音符、SPREAD で右が 1 段長い、FILTER は 1 次の LP（左）/ HP（右）、FREEZE
  - STYLE DELAY = Replika GR: 7 段の音符＋付点・3 連、5 スタイル、STEREO（モノ / 少しずらす / ピンポン）、ダッキング
  - TAPE HEADS = Tape Echo: こだまだけの記録は「つまみを変えた記録 − ECHO VOL 0 の記録」（スプリングとドライが同じなので引き算で消える）。
    左右で別々の輪・ヘッド A / B の位置 L = HM (1 − ST)、R = HM + (1 − HM) ST・戻り 1.2552 × FB・輪の中の音色（HP2 54.6 Hz Q 1.05、LP2 7.88 kHz Q 0.3、
    ECHO BASS = HP2 + g·LP2 の 5 点の表（谷が動く）、ECHO TREB = 零点 2042 Hz の 1 次の棚）・テープの速さの棚（零点 1919 Hz、−6.43 dB × log2(基本 / 166.7 ms)）・
    出口の音色（HP1 162 Hz・BASS 中点 159 Hz・TREBLE 中点 4.46 kHz）・HEADROOM = tanh(d x) / d（d = 7.1 e^(−4.02 h)、インパルスとノイズの塊の 2 点で決めた）・
    WARBLE = 2.3 ms × e^(7.5 (w − 1))。音色の合い具合 2 乗平均 0.54 dB。スプリングは 0.1.0 の SPRING（LENGTH 0.29・BASS 0.542・LOW DAMP は長さに合わせて・HIGH DAMP 0.2）
    ＋2.2 kHz −3 dB と 6 kHz +1 dB の補正、T60 は setT60Override（SPRING の部品としては使わない）で 0.3〜10 秒、REV TIME ごとの量と長さの補正表。
    20 本のうち DROPOUTS・NOISE・ACCELERATION・SPRING LENGTH を外して 16 本
- プチッの直し（自分の検査、scratchpad/dclick.cpp。ふつうとこだまだけの両方で、どのつまみも目安 10 未満にした）:
  TIME は 2 つの読み口を 40 ms でクロスフェード（XTap。BBD と TAPE HEADS は本物どおり音程がすべる）/ 輪の中・出口のフィルターは TPT の形
  （TptShelf1・Svf。setOnePoleLP / HP・RBJ の棚の Q で、合わせ込んだときの OnePole・Biquad と伝達関数は同じ。差 2e-6 以下）で、なめらかにしたつまみから 32 サンプルごと /
  スタイル・DIFFUSION・ピンポン・CROSS・ビット落としは、いつも回して混ぜ具合で入れ替える / WIDE の MOD DEPTH の量の補正もなめらかに / TAPE HEADS の REV TIME は 0.3 秒で /
  TAPE HEADS のこだまの出口は 0 dBFS までは素通し、その上はなめらかに +12 dBFS で頭打ち（音色を全部上げて発振させたとき +46 dBFS になっていた）/
  揺れ（WowFlutter）を reset で最初に戻す（TAPE HEADS が reset のあと新品と違った）
- **Svf の止め忘れ（自分の検査で見つけた、危ない抜け）**: Svf（VerbEngine.h）は ramp(false, 32) のあと止める処理がなく、step() が同じ歩みで足し続ける。
  VerbEngine は 32 サンプルごとに必ず ramp を呼ぶので大丈夫だが、ディレイの tone() は「変わっていなければ早く return」していたので、つまみを動かしたあと係数が流れ続けて
  数秒後に壊れた（ASan の fuzzall で TAPE HEADS が NaN 212 万サンプル。BASS の k = 21.7・m2 = −23）→ 早い return の前に Svf を ramp(true) で目標ちょうどに止める
  （BBD・STYLE・TAPE HEADS と EqParts.h の EQ 全部）。TptShelf1 は残り回数で止まるので元から大丈夫。Svf を使うときは、毎回 ramp を呼ぶか、止めること
- **DSP 検査 1 回目（0.4.0）「直してから」** → 直したこと:
  1. Svf 自体に残り回数を持たせ、n 回で目標ちょうどに着いて止まる（検査係の案。0.3.0 のリバーブの返りは 1 ビットも同じ、つまみを動かしても −115 dB 以下）
  2. TAPE HEADS の REV TIME のプチッがブロックで増える（512 で 17〜26、2048 で 50〜100）→ スプリングの長さを 32 サンプルずつ渡す（どのブロックでも 3 前後）
  3. WARP・CLUB・BBD の輪に直流がたまる（FB 100 % で −40 → −6 dBFS、BBD の発振の 2〜3 割が 20 Hz 以下）→ 戻りに直流止め（DcBlock、WARP・CLUB 5 Hz、BBD 10 Hz）
  4. CLUB の FILTER を初めて左へ回すと 20 ms −8 dB 落ちる（1 Hz のままだった）→ 外れている側の周波数は端（0.495 / 0.505 の値）に置く
  5. TAPE HEADS の発振が +12 dBFS に張り付く → こだまの出口の頭打ちを +6 dBFS に。STYLE の発振も WIDTH 次第で +10 dBFS を超えたので、出口に同じ頭打ち
  6. BBD・TAPE HEADS のすべりで読む位置が逆回り（−30.8 倍 / −5.0 倍）→ 遅れの伸びを 1 サンプルあたり 0.9、縮みを 3 まで（TAPE HEADS はいちばん遠いヘッドで）
  7. WARP・CLUB・STYLE の輪の入口の頭打ちが 0 dBFS で 3 次 −35 dB → softKnee2（0 dBFS まで素通し、その上はなめらかに ±2）。WARP は輪の入口が線形になって、
     FEEDBACK 100 % でゆっくり大きくなったので 1 周 0.9995 倍にした（→ 2 回目で原因は直流止めと分かり外した）
- **DSP 検査 2 回目（0.4.0）「直してから」** → 直したこと:
  1. 直流止め y = x − x₋₁ + R y₋₁ は高い音で 1/√R 倍（5 Hz で +0.003 dB）。CLUB の FEEDBACK 100 %・FREEZE は輪の利得がちょうど 1 なので、1 周ごとに育った
     （FREEZE・1/32 で 10 分 +21.5 dB）→ (1 + R) / 2 を掛ける（最大がちょうど 1）。直したあと 10 分で CLUB −23.7 → −23.8 dBFS、WARP も大きくならない
  2. WARP の 0.9995 倍は外した（4 点補間の大きさの最大はちょうど 1.0 で、育ちは直流止めの分だった）。FEEDBACK 100 % でまた鳴り続ける
  3. STYLE の CHARACTER が −17 dBFS より下で歪まない形（softKnee2 だけ）になっていた → 前の satSoft（±2 で飽和）を CHARACTER の 0〜25 % で混ぜ始める形に。
     CHARACTER 0 は素通しのまま、100 % は前と同じ（−20 dBFS で 3 次 −41 dB、−12 dBFS で −26 dB）。softKnee2 は安全のための頭打ちだけ
  4. STYLE の戻りにも直流止め 5 Hz（LO CUT 0・FEEDBACK 100 % で 20 Hz より下が −8〜−11 dBFS 鳴っていた）
  検査係の道具（scratchpad/dspinsp040）: longloop.cpp（FEEDBACK 100 %・FREEZE を 10 分）・warplong.cpp・gains.cpp（部品ごとの利得の最大）・charthd.cpp（CHARACTER の歪み）

## 0.5.0（2026-11-19）EQ・揺れもの
- 部品 35〜42。EQ は fx/EqParts.h（Svf と TptShelf1、なめらかにしたつまみから 32 サンプルごと。RBJ と伝達関数は同じ）、揺れものは fx/ModParts.h。
  測り方: EQ は cap/mod の 1/48〜1/96 オクターブの返りに RBJ のピーク・棚を当てはめ（作業メモは scratchpad/eq_laws_050.md）。揺れものは 440 Hz の正弦波の成分と、
  インパルス列 modiS（50 ms ごと 8 秒＋左右交互 4 秒、capone3m.sh・run_extra3m.sh、カテゴリ modi）の 1 発ずつの返り
  - SHELF EQ = EQ Shelving / CONSOLE EQ = Solid EQ（OUTPUT の初期値 1 = +12 dB は本物どおり。MODE E / G の違いと、大きく持ち上げたときの飽和はまだ）/
    PARAM EQ = EQ Parametric / GRAPHIC EQ = EQ Graphic / SCOOP EQ = Custom EQ。本物との差の中央値 0.18〜0.47 dB（SCOOP は TONE の端で 1.6 dB）
  - TREMOLO = Tremolo: 四角い波を上がり・下がりの時定数でなまらせる（時定数 = 周期 × 0.00592 × (e^(5.4 v) − 1)）。形の差 0.1〜0.7 dB
  - RING MOD = Ringmod: 運び手 log10 f = 1.9988 + 0.65414 v + 1.26984 v²。運び手は 1・2・3・5・6・7 倍音（4・8 倍音がない形）、RAW で
    r = 1.25 v − 0.25 v² 倍、1 倍音と A（元の音）は RAW の 2 次式。PITCH ごとの倍音の差は表（8.4 kHz で 1 倍音 −9.6 dB）。成分の差 0.27 dB 以内
    （山の前後 ±4 Hz のエネルギーで読む。最大の 1 点で読むと 0.6 dB 低く出る＝窓の端の分）
  - FLANGER = Flanger: 揺れ = 四角い波 → 1 次のローパス 2 段（fc = 0.993 × min(速さ, 3.28 Hz)）。3 倍音 6.6 %・位相 −0.92 rad まで一致し、
    3.28 Hz から上で浅くなるのも同じ（5.6 Hz で半分）。遅れ = 基本 + 1.745 S + S / 0.3158 / 2 × なまった四角、S = Smax × WIDTH²。
    MODE 0 / 1 / 2（GR の Mode は 0・1・2 の 3 段。0.25〜0.75 は 0 と同じ）= 基本 4.41 / 1.40 / 1.39 ms、Smax 3.36 / 2.48 / 3.98 ms。
    こだま = −HP1 500 Hz × (0.084 + 1.081 I × LP1)、ドライ = 1 次の高い棚（INTENSITY で低い所が下がる: 0.933 − 0.486 I）。
    戻りは HP だけ通した音（本物は 2 回目以降も高い音が減らない）で −0.093（MODE 2 −0.78）。STEREO は右のこだまを逆向き・ドライ × 1.08（MODE 2 は 1.86）。
    基本の遅れは XTap で 40 ms のクロスフェード（MODE の切り替えでプチッとしない）、ドライの棚・こだまの LP はなめらかにした値から 32 サンプルごと
- 自分の検査: ブロック差 0・reset は新品と同じ・ASan＋UBSan の fuzz で NaN 0・プチッの目安は FLANGER の MODE / INTENSITY を直して 2.4 以下（直す前 29.9 / 18.4）。
  EQ を全部上げきると SCOOP +27・SHELF +54.5・PARAM +59・CONSOLE +71 dB、GRAPHIC は帯域を詰めると +98 dB（OUTPUT 込み +109 dB）。ラックの頭打ち（+24 dBFS）にかかる（本物も上限なし）
- **DSP 検査 1 回目（0.5.0）「渡してよい」**。直したほうがいい所 7 つを入れた:
  1. TREMOLO / FLANGER（と 0.6.0 の ELECTRIC FLANGE）の SYNC を曲の位置にそろえる: Module に setTransport (ppq, playing)、processBlock で getPpqPosition、
     Rack が毎ブロック渡す（モノの出口は区切りごとに ppq を進める）。SYNC ON・再生中は、ブロックの頭で ph = frac(ppq / 音符の長さ)（TREMOLO は + duty = 拍の頭で上がっている側）
  2. GRAPHIC EQ の最大の持ち上げ（+98 dB）を README・NOTES に
  3. FLANGER の DEEP / RINGING の INTENSITY は測っていなかった（0.933 − c I と同じ形を仮定していた）→ cap/list17.tsv（MODE 1・2 × INTENSITY 5 点、WIDTH 0）を測って表に。
     DEEP は低い所が 0.969 → 0.060（100 % で −24 dB）、RINGING ははじめから 0.349。こだまは INTENSITY でほとんど変わらない（−1.2 / −1.8 dB）。RINGING の LP は 8 → 13 kHz で開く。差 0.16 dB（最大 0.8 dB）
  4. EQ の 1 次のなめらかが float の丸めで目標の手前で止まる（192 kHz で PARAM EQ の Q 40 の肩が 0.79 dB ずれた）→ 32 サンプルごとに目標の 0.1 % 以内なら目標ちょうどへ（settleTo）
  5. テンポ: ラックで 5〜999 BPM だけ受ける（inf で揺れが止まっていた）。TREMOLO / FLANGER の下限 20 → 5 BPM
  6. RING MOD の運び手を掛けた分（c x）にだけ直流止め 10 Hz（入口と運び手が近いと 20 Hz 未満の差の音が −5 dBFS 出ていた → −35 dBFS）
  7. RING MOD の DRY/WET を GAIN MATCH のセンドの決まりから外した（100 % ウェットでトラックに挿すのがふつう）
- **DSP 検査 2 回目（0.5.0）「渡してよい」**（ラックを通した SYNC のできごと 10 通り・区切り・おかしなテンポ / ppq の fuzz 38,010 ブロック・5 BPM で NaN 0、プチッはふだん以下）。入れたもの:
  1. TREMOLO の setTransport で sRate.cur = sRate.target（SYNC 中にテンポを変えた直後、古い速さで角を越えてから次のブロックで引き戻されて角が二重になった。
     ブロック 512 / 1024 / 2048 で 180 通り中 18 / 6 / 36 回 → 0 回。FLANGER は 2 段のローパスでなまるので要らない）
  2. FLANGER のドライの棚の gH を、デジタルの棚の x = tan(π 16000 / sr) / tan(π 角 / sr) で逆算（アナログの 16000 / 角 だと 16 kHz が表より 0.4〜0.6 dB 大きかった）
  3. README に「DEEP / RINGING では INTENSITY 0 でもうねりは強く、主に低音の量が変わる」。settleTo のコメントの数字
- コーラス系（Choral・Ensemble・Electric Lady・Vintage Vibrato）はつまみの効き方が複雑なので、追加の測定（cap/list15.tsv、59 件）を取ってから次の版で。
  分かったこと: Electric Lady = 三角の揺れ（最小 0.235 ms 固定、最大 3.37 + 6.09 × DEPTH ms）を 1 次のローパス 2.32 Hz に通す、RATE 1 = 11.4 Hz（50 ms ごとの記録では 8.6 Hz に折り返して見える）、
  COLOR = くり返し（0 → 0、1 → 0.68）＋音量（0.5 から上で下がる）、MIX 0 でドライ +5.6 dB。Ensemble = MODE 0 は三角の揺れ（速さ 0.1 + 2.9 × INTENSITY Hz、振れ ±1.29 ms）、MODE 1 はビブラート（6.1 Hz）。
  Choral = 揺れの傾きが一定（AMOUNT² × 16.5 ms/s）で、RATE が速いほど浅い。Vintage Vibrato = 6 Hz 前後のビブラートで、MODE が深さ（0 → ±0.18 ms … 5 → ±0.65 ms）

## 0.6.0（2026-11-19）コーラス系
- 部品 43〜46（fx/ChorusParts.h）: ELECTRIC FLANGE = Electric Lady / MULTI CHORUS = Choral / VIBE ENSEMBLE = Ensemble / SCANNER VIBE = Vintage Vibrato。
  測り方: インパルス列 modiS の 1 発ずつ（cap/modi、追加 cap/list15〜16.tsv）。比べる道具は scratchpad の chcmp.py（遅れの山・ドライ・声の大きさ）・chwarp.py（曲がった三角の当てはめ）・
  enfit.py / entone.py / entfit.py（Ensemble）・vvgd.py（Vintage Vibrato の群遅延）。本物の揺れは処理を始めて 0.504 秒後に記録が始まる位置で合わせた（こちらは 0 から）
  - 本物の記録は左の入口だけ（ギター用。右だけの入口は無音）→ 左右の入口を別々に扱うのはこちらの決め（左の入口 → 左寄りの声）
  - ELECTRIC FLANGE: 揺れ = 三角 → 1 次のローパス 2.32 Hz、くり返しは混ぜた音から（中 = a x + b v、v = 遅れ(x + g HP150(a x + b m^(−e) v))）。MIX × COLOR 25 通りで 5〜15 % 以内
  - MULTI CHORUS: 三角は少し曲がる（遅れ = 最小 + W (a u + (1 − a) u²)、a = 0.80 / 0.99 / 0.90 / A 1.0・B 0.80）。速さ = 0.25 + 7.4 RATE³、傾き = 0.173 + 17.31 AMOUNT^2.01 ms/s、
    最小 = 0.305 + 15.0 DELAY ms。MODE 0〜3（4 以上は 3 と同じ）の音色は 2 次 ＋ 1 次のローパス（21 点で 0.1 dB 以内）、LUSH は速さ 4.1 倍のサインが A・B 同じ向きに足される。
    VOICES の 2・3 組目は 4.0 / 8.11 ms 後ろ、速さ 1.4286 / 1.7265 倍、広がり 0.716 / 0.431 倍、3 組目だけ明るい（9821 Hz）。FEEDBACK = 声ごとの輪、FEEDBACK^0.25（0.99 まで）。
    1 発ずつの遅れの差 0.7〜1.2 サンプル（AMOUNT 1 で 3）、ドライ・声の大きさ 1 % 以内。SCATTER は本物でも変わらない（INVERT は 1 以上で ON）
  - VIBE ENSEMBLE: MODE は 0 = CHORUS / 1 以上 = VIBRATO の 2 つ。CHORUS = まっすぐの三角、幅 2.60 ms、速さ 0.1 + 2.9 INTENSITY。VIBRATO = サイン、3.243 × 3.565^RATE Hz、
    幅 (0.0385 + 0.522 DEPTH) ms × (6.123 / 速さ)^0.90。右 = 左 + 90° × STEREO。ドライだけ 26 Hz の 1 次ハイパスと 10 kHz −1 dB（ウェットには掛からない）。
    BASS / MID / TREBLE は本物が強く影響し合う（list18 の 12 通り・表: BASS 1 で MID が効かない、MID 0 では BASS で全体の大きさだけ変わる。
    つまみごとの棚・ピークの足し算は 5〜8 dB ずれ、ふつうのアンプの音色回路（3 次の式）でも 5 dB ずれた）→ 5 × 5 × 5 = 125 通り（cap/list19.tsv）を測り、
    「全体 ＋ 低い棚 150 Hz・ピーク 300 / 600 / 1200 / 2400 / 4800 Hz（Q 0.9）・高い棚 7 kHz」の dB の表（fx/EnsembleTone.h、scratchpad/engrid.py → engrid_header.py で自動生成）
    ＋ 2 次の LP 9 kHz Q 0.5。間は 3 つのつまみの直線補間。各点 0.3〜1.4 dB rms（TREBLE 0 の −40〜−60 dB の所で最大 4.9 dB）、組み合わせの 12 通りで 1 dB 以内。
    本物の VIBRATO は DEPTH 0.25 / 0.75 で中心の遅れが 0.3 ms ずれる記録があった（理由は不明。いちばん多い 3.16 ms にした）
  - SCANNER VIBE（大まか）: ウェット = 揺れない道 e(BLEND) ＋ 揺れる道 l(BLEND) ＋ 高い音のもれ（10 kHz から上 −7 dB）。揺れる道は遅れがサインで動き、遅れが長いほどこもる
    （2 次 2 段 12 → 3.3 kHz）。群遅延の幅は MODE 0〜5 で 20.6〜81.3 サンプル、COLOR 1 / 2 で 2.33 / 2.38 倍（GR の Color は 0・1・2 の 3 段、Mode は 6 段）。
    STEREO は本物でも変わらない → OFF で入口を左右まとめる形にした。群遅延の幅 1 サンプル以内、平均の音色 2〜3 dB 以内（COLOR の音色の違いは まだ）。
    本物は LC の遅れ線から 2 か所を混ぜて読むような形（早い所と遅れて来るなまった山の 2 つ）→ 細かい測定で遅れ線の形にする
- 自分の検査: ブロック差 0・reset は新品と同じ・44.1〜192 kHz で NaN 0・CPU 0.2〜0.6 %（96 kHz）。VIBE ENSEMBLE の TREBLE / MID のプチッ 36 / 14 →
  つまみをなめらかにして 32 サンプルごとに係数（4.0 以下）。音色の表にしてから、全部のつまみを 1 にすると揺れた音 +27 dB（本物と同じ。足し算の版は +35 dB だった）
- **DSP 検査 1 回目（0.6.0）「直してから」** → 直したこと:
  1. VIBE ENSEMBLE の SYNC: 再生中（SYNC を入れる・音符を変える・ループで戻る）に揺れの位置が一気に飛んでプチッ（目安 38〜303）。揺れにならしがないため
     → 曲の位置との差（±0.5 周）を 50 ms で埋める。遅れ線が空（入口が遅れ線の長さ以上 0 のまま）のときと、差が 1e-4 周未満はそのまま合わせる → 1.0〜1.4
  2. MULTI CHORUS で FEEDBACK > 0 のとき、VOICES で組を足すとプチッ（FB 0.75 で 23〜24）。読み飛ばしていた組の古い中身と戻りが、組の音量のならしを通らずに輪へ入った
     → 使っていない組もいつも計算する（CPU は VOICES 0 で 0.19 → 0.45 %）→ 3.7〜5.1
  3. SCANNER VIBE の STEREO スイッチ（左右が違う音でプチッ 34〜328、MIX 0 でもドライが左右まとめられていた）→ まとめる量を 30 ms でならす、ドライは元の左右 → 1.0
  4. 輪の頭打ちが ±1 からで、鳴く設定では −12〜−18 dBFS でも歪んだ → 本物を測った（cap/list20.tsv: −12 dBFS の 440 Hz を鳴きの山に。ELECTRIC FLANGE は倍音 −98 dB 以下、
     MULTI CHORUS の FEEDBACK 1 は 3 倍音 −91 dB = 歪まない）→ ±4 から（4 × softKnee2(x / 4)）。こちらの 3 倍音 −34 → −134 dB
  5. VIBE ENSEMBLE の SYNC が本物の速さの範囲を超えたとき: CHORUS は 3 Hz より速いと幅を 3 / 速さ 倍、VIBRATO の幅の式は 3.243 Hz より遅い所へ伸ばさない
- **DSP 検査 2 回目（0.6.0）「渡してよい」**（SYNC 12 通り 1.0〜1.5、音色の表 68,921 通りで補間のふくらみ +0.07 dB・係数の異常 0、VOICES 1.2〜2.1、STEREO 1.0〜1.5、
  輪の中は最大 ±8 で止まる、+24 dBFS で NaN 0）。入れたもの: SYNC の「頭」を reset からの時間（0.2 秒）でなく「遅れ線が空か」で決める（止めてすぐ再生し直すと
  音が流れたまま飛んでプチッ 108〜253 → 1.2 以下。頭からぴったり合う）。
  輪の頭打ち: 本物を鳴きの山の真ん中でも測った（cap/list21.tsv: FEEDBACK 1・AMOUNT 0・DELAY 0.112〜0.124 で 440 Hz −15 dBFS rms → 出口 +1.6〜+3.2 dBFS、
  2 倍音 −41〜−50・3 倍音 −63〜−68 dB）。こちらは ±4 だと −10 dBFS で 3 倍音 −27 dB に潰れていた → MULTI CHORUS の輪は ±16 から、1 周は 0.99 まで
  （−5 dBFS で 3 倍音 −104 dB 以下。本物より鳴きが約 7 dB 弱いが、0.995 にすると −1.7 dBFS で 3 倍音 −46 dB になり余韻も長いので 0.99 にした）。ELECTRIC FLANGE は ±4 のまま（本物も歪まず、輪は 0.805 まで）

## 0.7.0（2026-11-19）フェイザー・テープ
- 部品 47〜50: CLASSIC PHASER = Phaser Nine（fx/PhaserParts.h）/ SWEEP PHASER = Stoned Phaser（大まか、同）/ TAPE WOW = Tape Wobble（fx/TapeParts.h）/ ROTARY = Rotator（大まか、fx/RotaryParts.h）。
  作業メモは scratchpad/phaser_laws_070.md。比べ方: notch.py（谷を 1 発ずつ）・phsum.py・p9fc.py（全通過の角を 1 発ずつ当てはめ）・spfc.py・twcmp.py（440 Hz の揺れ・3 倍音・音色・雑音）
  - CLASSIC PHASER: 反転する 1 次の全通過 M 段（NOTCHES = 段の数: 0〜2 → 2、3〜8 → 3〜8、8 以上 → 8）、くり返しは最後の段 → 3 段目の入口（輪 = M − 2 段）を遅れなしで解く。
    COLOR → 戻り −0.96 COLOR^1.15。出口 2 [(1 − DW) x + DW wet]。揺れ = log2 の三角を 1 次の LP 3.2 Hz に通す、速さの表、中心 8.93 + 4 FREQ、幅 4 DEPTH、上は 5 kHz で頭打ち。
    1 発ごとの大きさの差の中央 0.4〜0.7 dB（rms 1.6〜2.5 dB、谷の時刻のずれを含む）。NOTCHES の切り替えは 100 ms のなめらかなクロスフェード（新しい組のくり返しは 0 から上げる。状態は組ごと）。
    奇数の段で COLOR を上げると直流〜100 Hz が持ち上がる（輪の直流の利得 +0.96 × COLOR^1.15。本物も COLOR 1 で 3 Hz +25 dB・20 Hz +20 dB）→ 揺れる側の入口に 10 Hz の 2 次の HP（20 Hz から上は 0.3 dB 以内）
  - SWEEP PHASER: ふつうの全通過 M 段（NOTCHES 0・1 → 2、n → 2n、5 以上 → 10）、輪は鎖全体 −0.141、出口 (1 − MIX) x + 1.38 MIX wet。速さ 0.01 × 1000^RATE、
    下限 2^(6.18 + 4.54 MIN)、上限 2^(10.86 + 4.46 MAX)。中央 0.6〜1 dB。COLOR ON は形が合っていない（仮に最後の 1 段の輪 +0.10、3.7 dB）。MODE・INVERT は本物でも変わらない。
    SYNC は曲の位置との差を 50 ms で埋める（入口が 50 ms 以上無音のときだけ一気に合わせる。VIBE ENSEMBLE と同じ）
  - TAPE WOW: 遅れはほぼ 0 から（0.0496 ms ＋ WOW 10 ms × WOW² (1 + sin) ＋ FLUTTER 1.23 ms × FL² (1 + sin) ＋ SCRAPE × (0.4 ms ＋ 雑音)）。0.0496 ms は読み出しの 1 サンプルと合わせて
    3.38 サンプル = MIX 0.5 の谷が本物と同じ 7 kHz（0.07 ms のときは 5.5 kHz）。SCRAPE の雑音は 1 次 LP 60 Hz × 2 次 LP 130 Hz、量は SCRAPE に比例
    （揺れのスペクトル 1〜2 dB、rms 0.150 / 0.077 / 0.041 ms ← 本物 0.150 / 0.077 / 0.039。最初の版の 42 Hz の帯域通過・2 乗は上に広がりすぎた）。
    乱数の雑音は √(sr / 48000) 倍（サンプルレートで聞こえる所の密度が変わらない）。歪み tanh(d x)/d × 持ち上げ の表（4 倍の中で。ドライも同じ 4 倍のフィルター、遅れ 4 を申告）、
    歪みと TAPE AGE の 1 次の LP、AGE の 2 次の HP（AGE 0 では通さない）、雑音（AGE 0 −66 dBFS、AGE とともに別の雑音 −91 / −80 / −72）、MIX は等パワー。
    440 Hz の揺れ・3 倍音・音量はほぼ一致、音色 1〜3 dB、雑音 ±1 dB。揺れの深さは 0.25 秒でならす（WOW 0 → 1 のプチッ 16.9 → 2.7）
  - ROTARY（大まか、fx/RotaryParts.h）: LR4 800 Hz でホーン / ドラムに分け、それぞれの回転でドップラー（ホーン ±0.41 ms・ドラム ±0.8 ms）と音量の揺れ（DISTANCE で 6.5 → 2.2 dB / 3.0 → 1.4 dB）。
    速さの式（T FAST 1.45 + 10.35 v、B FAST 1.34 + 9.23 v、SLOW は 0.109 / 0.111 倍）、回り始めは遅い速さから（ACCEL の時定数 ホーン 4 → 0.06 s、ドラム 12 → 0.15 s。
    作った直後だけ。ホストの reset では今の SPEED の速さから）。
    キャビ ON / OFF の音色はウェットの平均を当てはめた（1.2 / 0.8 dB rms）。CAB 1〜45 は全部同じ音（2 つだけ）。CAB OFF では揺れ 0.2 倍（本物と同じ）。
    1 発ごとの比べ（rot.py）: 速さ ±0.2 Hz、揺れの深さ・平均の大きさ 1〜3 dB。マイクの左右の違い（本物は右のホーンの揺れが小さい）はまだ
- **DSP 検査 1 回目（0.7.0）「直してから」** → 直した:
  1. SWEEP PHASER の SYNC: ブロックの頭で位置を一気に合わせていた（ループ・音符・再生開始でプチッ 82〜1,316）→ 曲の位置との差を追いかける（入口が 50 ms 無音のときだけ一気に）→ 1.0〜2.7
  2. CLASSIC PHASER: 奇数の段で COLOR を上げると直流〜20 Hz が最大 +27.6 dB（輪の直流の利得 +0.96）。本物も 3 Hz +25 dB → 入口に 15 Hz の 4 次 HP（2 回目で置き場所を直した）
  3. CLASSIC の NOTCHES の切り替え（COLOR 1 で 3→8 段 19.6）→ 100 ms の smoothstep ＋ 新しい組の戻りを 0 から → 全部 1.0（SWEEP も同じ形）
  4. TAPE WOW の歪みに 4 倍がなかった（SAT 1・7 kHz −6 dBFS で折り返し −9.1 dB）→ Oversampler4（ドライも通す、遅れ 4）→ −40.6 dB
  5〜9. 乱数の √(sr/48000)、でたらめの係数、STEREO を 0.25 秒、SCRAPE を本物の揺れのスペクトルに（LP 60 × LP 130、量は比例）、いちばん短い遅れ 0.0496 ms（MIX 0.5 の谷 7 kHz = 本物）
  ほか: ROTARY が reset のたびに遅い速さから加速し直していた、README（内蔵ラックの音量・1.9 kHz の山）、コメントの食い違い
- **DSP 検査 2 回目（0.7.0）「直してから」** → 直した:
  1. CLASSIC の 15 Hz HP を揺れる側だけに入れていたため、ドライとの位相がずれて初期値で 16〜25 Hz +6〜8 dB、COLOR 0 の奇数段で 80 Hz −26 dB の谷
     → 入口で 1 回だけ通し、ドライにも同じ音を使う（本物との差 20〜300 Hz の平均 0.65 dB・最大 1.00 dB = 直す前の 0.61 / 1.08 と同じ水準、直流 −118 dBFS）
  2. ROTARY が prepareToPlay（ラックは部品を全部作り直す。書き出しへの切り替えでも呼ばれる）のたびに遅い速さから加速 → Module::startFresh() を足し、
     画面で部品を選んだとき（Rack::service）だけ遅い速さから（ラックが動かし始めに呼ぶ reset で消えないよう、最初の process まで印を残す）
  3. ホストの reset（ラック）: ドライの遅れの記録を消していたので、遅れのある部品を OFF にした段でドライが抜けた（MAXIMIZER 1,199）。ON のままでも中を消すので段（CLASSIC 697 など）
     → 記録は消さない、遅れのある部品が ON の段は先読みが埋まるまでドライ（動かし始めと同じ）、出口は reset 前の最後の音とその傾きから 2 ms でつなぐ → どの部品も 3.8 以下
  4. SWEEP の追いかけの時定数を 0.1 / 揺れの速さ（0.05〜2 秒）に（0.5 Hz の揺れで上乗せ +9.9 → +1.65 Hz）
- **DSP 検査 3 回目（0.7.0）「直してから」** → 直した（検査係の案 prop5 を本体へ。MAXIMIZER の見分けは Module::hasCeiling() にした）:
  1. ラックの reset のつなぎ: 傾きの外挿が高い音で行き過ぎた（空のラックでも 7 kHz 0 dBFS で +15 dBFS、ファズで最大 +28 dBFS）→ 外挿を出口の山の包絡（10 ms で減る）で頭打ち、そのあとに ±16
  2. reset のたびに MAXIMIZER の段がドライを 15 ms 通して天井を超えた（+3.7〜+5.2 dBFS）→ 天井のある部品（hasCeiling）の段はドライを混ぜない（先読みが埋まるまで無音 → 2 ms でフェードイン）
  3. 無音からの reset でも頭 2 ms をフェードインしていた（キックの最初の 1 ms の山 0.838 → 0.419）→ 入口に音が来ていた（−80 dBFS 以上 = flowing）ときだけ段のゲート・入口のフェード・ドライの混ぜ、
     出口が鳴っていた（−50 dBFS 以上 = sounding。TAPE WOW のサーッは −54）ときだけつなぐ
  4. 音を流したままの reset で、遅れの線のある部品からあとで段が出た（0.1.0 から。TWIN DELAY −40 dBFS など 25 部品）→ flowing のときだけ響きのある部品の入口を 15 ms でフェードイン。
     段のゲートはドライだけでなく段の出口全体に → あとの段はどの部品も −66 dBFS 以下（TAPE WOW のサーッと同じ）
  5. SWEEP の追いかけが一瞬 4〜6 倍の速さ・逆回り → 1 サンプルの補正を本来の速さの −0.5〜+1 倍で頭打ち（最大 2 倍・逆回りなし）
  6. prepare で dryGate を戻す。（7. プロジェクトを開いたとき状態を戻す順番しだいで ROTARY が 1 回だけ遅い速さから回る、は確かめられていない）
  ラックのファズ（reset 2 万回・選び直し 3,500 回、ASan）: NaN 0・±16 超え 0。MAXIMIZER の天井超え 0、最初のアタックは reset なしと同じ
- **DSP 検査 4 回目（0.7.0）「直してから」**: 3 回目の 6 件は閉じた。新しく 1 件 → 直した:
  停止中・バイパス中にホストが process を呼ばないと、reset のつなぎ（flowing / sounding / 最後の音と傾き）が止めた瞬間の古い値で動き、再生の頭にプチッ・頭の削れ（キックの頭 0.500 → 0.000、
  SPACE VERB の頭 15 ms −4.5 dB）。→ 最後の process から 100 ms（か 4 ブロックの長いほう）たっていたら「途切れた」とみなし、つなぎ・段のゲート・入口のフェード・ドライの混ぜを用意しない
  （reset のあと最初の process までに途切れたら取り消す）。時計は std::chrono::steady_clock（ロックなし）。途切れがあるとき = まっさらの出だしと差 0、ふだんの再生（実時間のペース）では今までと差 0。
  hasCeiling は今ある部品では MAXIMIZER だけ（遅れ 100 以上も MAXIMIZER だけ）。先読みのあるリミッターを足すときは true を返すこと
- **DSP 検査 5 回目（0.7.0）「渡してよい」**。勧められた小さな直しを当てた: reset が process をはさまずに 2 回来て、その前後に止まっていると、遅れのある天井なしの 9 部品（コンプ類 8 種と TAPE WOW）の段が
  再生の頭 15 ms だけドライ → 部品の音へ移った（2 回目の reset が wetBeforeReset に 0 を写す / 途切れに気づいても 1 回目の prime を片付けない）→ 続けて来たときは最初の wet を残し、途切れていたら先に片付ける。
  reset が続く 3 通りの順番 × 部品 × 入口 48 通りがまっさらと同じ（dblreset）。ラックのファズ（ASan）NaN 0・±16 超え 0

## 0.8.0（2026-11-19）フィルター・ワウ
- 部品 51〜55（fx/FilterParts.h）: MORPH FILTER = Pro-Filter / HOWL WAH = Cry Wah / VINTAGE WAH = Real Wah / CUSTOM WAH = Wahwah / VOWEL WAH = Talk Wah。
  作業メモは scratchpad/filter_laws_080.md。比べ方: ピンクノイズの区間の伝達関数（tf.py、遅れ 16 で合わせる）を 2 次などの式に当てはめ（bqfit.py・pbp.py・wahjoint.py）、
  libmodi（modilib2.cpp）で同じ modS を通して全部の記録と比べる（wahcmp.py・cwcmp.py・vwcmp.py・mfcmp.py）。追加の測定 list22（ワウ 2 種の途中の位置）・list23（Wahwah 132 通り）・list24（Talk Wah・Formant Filter・Peak Filter 82 通り）
  - MORPH FILTER: 状態変数フィルターの LP / BP / HP を MODE で直線に（LP → −BP → HP）、SLOPE で 2 段目（LP²・BP²・HP²）へ混ぜる量、RES の Q と全体の大きさの表。
    2 倍（ハーフバンド 1 段）で計算（本物は角を上げきったとき高い所がなだらか）。遅れ 3 を申告。45 通りで中央 0.70 dB。
    合っていない所: RES 0.75〜1 の山（本物 700 Hz に +28 dB の鋭い山、こちら +18 dB で広い）、FREQ 1 の BP / HP の 10 kHz から上（ラダー型でも RES の端は 2.4 dB）
    次に詰めるときの手がかり: 本物の 4 次を自由な分子で当てはめると（0.07〜0.10 dB）、極は同じ角（710 Hz）に Q の違う 2 組（RES 0 / .5 / .75 / 1 で 0.41・0.64 / 2.97・0.94 / 8.68・1.29 / 60・1.67）。
    分子 ≈ G ((1 − m) D_b ＋ m)（D_b = 低い Q の組）= 1 段目が高い Q・2 段目が低い Q、SLOPE で 2 段目へ混ぜる形。こちらは 2 段とも同じ Q（pf2.py）
  - HOWL WAH / VINTAGE WAH: 全体の大きさ × 2 次（分子自由）× 2 次 × 1 次の HP。9 か所（WAH 1/8 ごと）をまとめて当てはめ、係数の対数が位置でなめらかに動くようにした（2 階差に罰）。
    HOWL は HP なし（6 Hz まで平ら。1 Hz の直流よけだけ）、VINTAGE は 200〜235 Hz の 1 次 HP。周波数特性 0.03〜0.12 dB、本物の遅れ（+1 サンプル）を除いた波形の差 −24〜−46 dB。
    ペダルは端から端まで最短 0.04 秒（一瞬で飛ばすと HOWL の 2 段の中の大きな分が追いつかず「チッ」27 → 1.3。0.08 秒だと全幅 8 Hz のオートメーションの振れが 59 % に削られた）
  - CUSTOM WAH: 出口 = DRY x ＋ k (αL LP ＋ αB BP ＋ αH HP)。周波数・Q・LEVEL は MIN と MAX の間を t(p) = p (2p − 1) ＋ 4p (1 − p) MID でつなぐ（t(0.5) = MID）。
    周波数はつまみの位置で混ぜてから Hz に（FREQ MAX は +0.399 ずれた位置 → Φ(u) の表）、k = 0.80 − 0.698 u、λ = 1.99 (3.17^u − 1)、α は MODE × λ の表、DRY = 2 × 1000^(v − 1)。
    197 通りで中央 0.10 dB・9 割 0.42 dB・いちばん悪い所 1.12 dB（つま先 10 kHz で Q が本物より 26 % 高い）
  - VOWEL WAH: 山で正規化した BP 4 本の並列（母音の F1・F2・F3 と 8〜10 kHz の広い山）。WAH の表は 1/16 ごとに 17 か所測った値、SIZE は F1 だけ 3.185^(SIZE − 0.49) 倍、
    OUTPUT ±15 dB、BRIGHT = f / 900 Hz の傾き（各帯域を HP × f_i / (900 Q_i) に替える ＋ 双一次の持ち上がりを (1 + 0.43 z⁻¹) / 1.43 で戻す）。56 通りで中央 0.26 dB・いちばん悪い所 0.48 dB
- **DSP 検査 1 回目（0.8.0）「直してから」**（発振・NaN・暴走はなし）→ 直した:
  1. 音が流れている最中のホストの reset で、遅れのある MORPH FILTER がコンプ類と同じ「先読みが埋まるまでドライ → 15 ms で部品の音へ」に入り、フィルターを通らない音が漏れた
     （LP 194 Hz・ノイズで 2 kHz より上 −104 → −18 dBFS）→ Module::dryAfterReset()（初期値 = ! hasCeiling）を足し、MORPH FILTER は false。ラックはこれで判断
  2. （任意）ペダルの最短 0.08 → 0.04 秒（WahPedal::kSweepSec、CUSTOM・VOWEL も同じ）/ CUSTOM WAH の RES の位置を 1 までに（MID の曲線で 1 を超えて Q が 50 に跳ねた。本物の記録は 1 の内側だけ）/
     表示を実際に山が来る周波数に（表の値はアナログの Hz。WahPedal::digitalHz）/ README に CUSTOM WAH の大きさと「挿しただけで +8〜9 dB」
- **DSP 検査 2 回目（0.8.0）「渡してよい」**: 段の漏れ（ラックのつなぎを外して）LP −18.5 → −57.8、BP −18.7 → −54.9、HP 3 k −32.0 → −54.5 dBFS。0.7.0 の 50 部品 × 27 条件で出力が 1 ビットも同じ。
  ペダル 0.04 秒で 8 Hz の追従 64.5 → 92.7 %、RES の頭打ちで Q の最大 50 → 9.8、表示と実際の山の差 3 % 以内。
  **次に直す（0.7.0 から、全部品に関わる）**: ラックの reset のつなぎ（最後の音と傾きを 2 ms 持つ）が、高い音だけの出口（HP・ハイハット）に低い「ポッ」を作る
  （MORPH HP 3 kHz で 300 Hz より下 −110 → −12.6 dBFS。つなぎなしなら −54.6）→ つなぎの元を低い所だけ（200 Hz 前後の 1 次 LP で追った値と傾き）にする案。
  直したら rackreset・swapin・max と resetleak（HP を足すなら、つなぎを外すか空のラックと比べる形で）を回し直す
  そのまま: VOWEL の BRIGHT の戻しは 44.1 kHz だけ 18 / 20 kHz で +1.9 / +6 dB（係数は 48 kHz 用）、HOWL の 1 Hz 直流よけの残り −75 dBFS、
  Polyphase2x は中の値を 0 に落とさない（processBlock の ScopedNoDenormals の中で使う。AIO Guitar へ移すときも同じ）
  - 0.9.0 へ回したもの: FORMANT FILTER（並列 BP では 0.9 dB、形がまだ分からない）、PEAK FILTER（PUMP が音量で動くらしい = 静かな測り方では決まらない。KILL は 1.76 × (1 − BP) の形）、
    AUTO FILTER・DOPPEL FILTER・FILTERBANK

## 0.9.0（2026-11-19）音量で動くフィルター（fx/FilterParts2.h）
- 測る信号は dynS（1 kHz の音量の階段 −60〜0 dBFS 0.15 秒ずつ・段 −40 → −10 → −40 / −30 → −6 → −30・ピンクノイズ −60 / −40 / −20 dBFS・DI）。list25〜27
- 比べ方: 階段の 1 kHz の複素の利得から角を逆算（afstair.py）、段で角を 1 ms ごとに追う（afstep.py）、ピンクノイズの区間で 2 次に当てはめ（afdyn.py・pklow.py）、
  自作は libmodi で同じ dynS を通す（tfcmp.py・tfstep.py・pfcmp.py・pflim.py）
- TOUCH FILTER = Auto Filter: 山を追う（ATTACK 9.5 × 13.6^A ms、RELEASE 表 75 / 169 / 322 / 388 ms）→ dB → c = clip(((L − T) / 12.3)^0.62 ＋ o(OFFSET), 0, 1)、
  T = −19.3 − 20.7 SENS、記録は c = 1 になる音量で頭打ち（本物は大きな音のあとすぐ戻る）。角 = 46 × 3.65^RANGE ... 952 × 4.08^RANGE（対数で c）、
  DOWN は 2727 → 272 Hz（30 ms でつなぐ）。分子は MODE の表（角 90 Hz と 1.8 kHz の 2 組を角で補間）、Q も 2 組。
  階段は 1 dB 前後、ATTACK / RELEASE の 10〜90 % はほぼ本物どおり。合っていない所: DOWN の曲線（本物はゆっくり）、OFFSET 0.5 以上の低い所の滑らかな立ち上がり
- PEAK FILTER = Peak Filter: 静かな所ではベル型 1 本（山の高さ R、幅 κ、base）× PUMP の音量。KILL は 1.76 (1 − d BP)。最後にリミッター（天井 0.655、効き 0.2 ms、戻り 0.3 秒）。
  49 通りで中央 0.11 dB・いちばん悪い所 1.5 dB。リミッターの効き始め・戻りもほぼ本物どおり
- まだ: FORMANT FILTER（並列 BP で 0.9 dB）、DOPPEL FILTER、FILTERBANK
- **DSP 検査 1 回目（0.9.0）「渡してよい」**（発振・NaN・メモリの誤り・遅れの申告違いなし。ASan / UBSan 誤り 0）→ 勧められた所を直した:
  1. README「出口はいつも −3.7 dBFS」→ 先読みのないリミッターなので音の頭は 1 ms ほど超える（Peak Push・−6 dBFS のギターで +1.5 dBFS、最悪 0 dBFS 入力で +13.8 dBFS が 1 ms 未満）
  2. GAIN MATCH: 最後の段が MIX を上げた PEAK FILTER のときも limitBoost(0)（PluginProcessor の lastLimits。上げると天井ごと持ち上がる）
  3. FREQ の表示を実際に山が来る周波数に（WahPedal::digitalHz。0.9 で 11.2 → 9.67 kHz）。KILL ON のときは FREQ を上げきっても「OFF」と出さない（高い所が削れるため）
  4. test_inspect の click で peakflt は「Peak Push」の設定から（MIX 0 は素通しで何も測れていなかった）→ FREQ 5.5・EDGE 6.4・PUMP 4.0・MIX 3.4・KILL 2.0
  そのまま（任意）: TOUCH FILTER は大きな入力で ATTACK を最短にすると角が 0.1〜0.3 ms で一気に動き、音の頭に短い高い音（山を直線の大きさで追うため。本物の 0 / +12 dBFS の段で比べてから）

## 0.9.1（2026-11-20）ラックの reset のつなぎを線形予測に（Rack.h、全部の部品に関わる）
- 0.8.0 の「次に直す」: 音が流れている最中のホストの reset で、最後の音と傾きを 2 ms 持ってつなぐと、高い音だけの出口（HP・ハイハット）では持った値が 2 ms の低い「ポッ」になった
- 直し方: 出口の最後の 2048 サンプルを輪に記録（hist）。reset のとき、最後の 10 ms（ハン窓、48 kHz で 480 サンプル）から線形予測の係数（自己相関法・Levinson-Durbin、
  次数 32 × レート / 48 k、16〜64。ラグ窓 60 Hz、白色ノイズ −50 dB、極を 20 ms で内側へ）を求めて続きを 2 ms 分作り（makeBridge、確保なし）、smoothstep で新しい音へ渡す（頭打ちは検査 1 回目の 1 番）。
  高い音だけの出口なら続きも高い音のまま、低い音なら波の形のまま続く（パケット損失の隠し方と同じ考え）。
  続きの頭打ち: 最初は 10 ms で減る包絡 envPk（220 Hz の次の山を平らに削って 5 kHz より上が −62 dBFS に増えた）→ 予測に使った 10 ms の山（検査 1 回目で角が残ると指摘）→ 直前約 48 ms の山 ＋ なめらかなゲイン
- 測った（直す前 → 後。検査係 0.7.0・0.8.0 の道具 hpdiag・rackscen・rstover・stale2 などをそのまま使って）:
  MORPH HP 3 kHz の reset で 300 Hz より下 −12.6 → −82.8 dBFS（つなぎなしだと −54.5、ふだん −110）/ HP 15 kHz の 2 kHz より下 −17.4 → −63.8（ふだん −61.7）/
  LP 194 Hz の 2 kHz より上 −76.8 → −100.1 / 220 Hz を流したままの場面 13 個 × ブロック 4 種のプチッ（5 kHz より上）: 悪くなったもの 0、多くは 13〜23 dB 静かに（MAXIMIZER ON で −78 → −94〜−101、CLASSIC PHASER −72〜−75 → −91〜−96）/
  reset 後 3 ms の山は全部ふだんの出口の山以下（CLASSIC PHASER 1 kHz の谷 −10.8 → −2.8 dBFS = 抜けなくなった）/ MAXIMIZER の天井・無音からの最初の音（firstfade・hissfirst・maxreset）は 1 ビットも同じ /
  test_inspect の rack・rack2・swapin・max・maxlow・block・reset・rates は 1 ビットも同じ。rackreset は 94 サンプル目までの山が −15.1 → −10.0 dBFS（つなぎが波の形のまま 2 ms で消えるため。長さは同じ）
- bridgefuzz（新しい検査）: レート 8 k〜384 k × ブロック 1〜2048 × 入力 9 種 × ラック 4 種で reset 2016 回（つなぎの途中・2 回続けても）、NaN・±16 超え 0、ASan / UBSan 誤り 0。
  reset 1 回の計算 48 kHz で 0.03 ms、192 kHz 0.23 ms、384 kHz 0.29 ms
- **DSP 検査 1 回目（0.9.1）「直してから」**（HP の低い「ポッ」が消えたのは確認・NaN なし・天井を守る・ASan / UBSan 誤り 0）→ 直した:
  1. 続きの頭打ちが「予測に使った 10 ms の山」で 1 サンプルずつ硬い clamp → 和音のうなり・トレモロ・ひずんだ音・リミッターの後で本当の続きより低く角ができ、
     いちばん悪い位置の reset のプチッ（5 kHz より上）が前の版より大きかった（MAXIMIZER ＋ ひずんだ和音 前 −39.1 → −18.2 dBFS、192 kHz のひずんだ和音 −11.7）
     → 頭打ちの基準を直前約 48 ms の実際の出口の山（1 ms ごとの山の輪 pkRing。どのレートでも同じ長さ。検査係の案は 2048 サンプルの輪全体 = 192 kHz では 10.7 ms しかない）、
     硬い clamp をやめ、必要な下げを前後に 0.25 ms の傾きでならしたゲインに（O(n)、|続き| ≤ 山は必ず守る）→ −27.9 dBFS（ふだん −46.6）、空のラックの和音 −36.7 → −73.3、
     192 kHz の和音の最悪 −50.4（検査係の案）→ −73.9
  2. 途切れ（process が 100 ms 以上来ない）のあとも記録と山の包絡が古いまま → process の頭で途切れていたら消す（clearHistory）
  3. reset の重さ（192 kHz で 0.24 ms）→ 自己相関を 4 本に分けて足す（0.13 ms、384 kHz 0.18 ms）
  4. rFade を makeBridge より前に出していた → 後に。さらにプロセッサーの reset() を processBlock と同じ鍵（getCallbackLock）の中で呼ぶ（別スレッドの reset と音の処理が重ならない）
  5. Rack の大きさ: 1,096 → 67,528 バイト（ヒープの上）
  確かめた: test_inspect の rack・rack2・swapin・max・maxlow・block・reset・rates・rackreset は直す前と 1 ビットも同じ、resetleak の MORPH LP 52 Hz だけ −89.4 → −88.8 dBFS。
  検査係の道具（pos・rk・ceil・bt）は 48 kHz で検査係の案と同じ結果、天井 −6 dBFS の MAXIMIZER で −6.000 dBFS。ASan / UBSan 誤り 0（bridgefuzz・bt の全部の場面）
  そのまま（まれ）: 0.67 ms 以内にぴたっと止まった音を reset すると −2.1 dBFS でよみがえる / 鳴り始め 0.25〜1 ms の reset は 5 kHz より上 −26〜−38 dBFS（前 −40〜−64）
- **DSP 検査 2 回目（0.9.1）「渡してよい」**: 1 回目の 5 点はすべて正しく直っている（longPeak を直近の本当の最大と照らし合わせ 71,801 回でずれ 0、500 Hz〜768 kHz・ブロック 1〜2048・
  prepare し直し・途切れ・reset を混ぜて。ASan / UBSan 誤り 0。getCallbackLock: JUCE の AU は processBlock を同じ鍵の中で呼ぶ・CriticalSection は再帰で優先度の継承つき、
  processBlock の中ではプラグインの鍵を取らないので止まる道はない。鍵を持つ時間は 48k で最長 0.03〜0.32 ms、192k の響き 8 段で最長 1.03 ms（ほとんどは部品の reset。0.9.0 でも中央 0.4 ms））
  → 勧められた 1 行を足した: 途切れたら途中だったつなぎもやめる（rFade = 1。reset → 2 ms 未満の process → 途切れ → 再開で、途切れる前の音の残りが最大 −3.7 dBFS 混ざった。0.9.0 でも −4.5 dBFS の前からの穴。
  undoContinuation は呼ばない = 部品を ON にした直後の先読みの埋めに古い wetBeforeReset を戻すため）。process の重さ 9.5 → 13.9 ns / サンプル

## 0.10.0（2026-11-20）フィルターバンク（fx/FilterParts3.h）
- 58 RESO BANK = Filterbank。記録 cap/mod/Filterbank 75 通り（インパルス 1 発の区間 2 秒とピンクノイズ）。どの設定も線形で時間で変わらない（コヒーレンス 1.000）
- 分かったこと（scratchpad/fb の fbimp.py・fbmodel.py・fbnum.py・fbeach.py・fbfinal.py）:
  - 帯域のつまみ 1 と 0 の差で 1 本ずつ取り出すと、1 帯域 = 山 2 本（中心の上下 ±0.3 オクターブ。BAND SPLIT 0 の記録では 1 本）。帯域のつまみに正比例（0.5 でちょうど半分）
  - 中心 C = 20 Hz × 5^SHIFT、左右は C × 2^(∓x)、x = 2 (LR SPLIT − 0.5) / 帯域の間隔 log2 R = 0.3334 + 1.0468 STRETCH（0.75 まで。1 で 1.331）/
    割れ幅 d = 2.5 × BAND SPLIT × log2 R / 1/Q = 0.603 (1 − RES)³ + 0.0033 / 大きさ G(RES) は 6 点の表（ln で補間）
  - 山の形: 普通の BP（双一次）では 3 kHz より上の平らなすそ（−14 dB、位相 −25〜−60°）が合わない（差 4.4 dB）。どの帯域のすそも同じ形で大きさは中心に比例
    → 分子 (1 − z^-1) の BP（= 0.5 k (BP ＋ g HP)、TPT の状態変数フィルターの出口の混ぜ）× 全部に共通の N(z) = s (1 − 0.37919 z^-1) / (1 − 0.08355 z^-1 ＋ 0.18263 z^-2)。
    遅れ 0 のまま位相も合う（7 記録をまとめて当てはめて差 0.36〜0.40 dB、位相 1〜4°）
  - MIX: smoothstep(MIX) でウェット、ドライは 0.968 (1 − smoothstep)。STEREO 0〜3: モノの入口では左右とも 1 ビットも違わない（違いは測れない）→ 0 = MONO（入口を混ぜる）、1〜3 = STEREO
- 本物と比べた（rbcmp.py、libmodi で同じインパルスを通して 20 Hz〜20 kHz の大きさ）: 75 通り × 左右の差の rms 中央 0.33 dB。
  外れ: STRETCH 1（左 2.5 / 右 4.8 dB）・BAND SPLIT 1 の右（2.9 dB）・RES 1（2.2〜2.4 dB。Q 300 の響きが記録の 2 秒に収まらない）・STRETCH 0.75 の右（1.1 dB）
  = 上の山がナイキストを超える設定。本物はそこで変わった形（930 Hz に −40 dB の谷・18 kHz に +16 dB）になり、消す・上限で止めるのどちらでも合わない → 20 kHz（0.45 sr）で止める
- 自己検査（scratchpad/fb/rbtest.cpp）: ブロック 1〜2048 の差 0、reset 後は新品と同じ、44.1〜192 kHz で NaN なし（RES 1: ギターふう −6 dBFS のあと 8 秒で −57 dBFS、10 Hz の山の設定は 30 秒で −73.5 dBFS。どれも減っていく。tailSeconds は一番低い山の −60 dB まで、最長 30 秒）、
  300 組 × 入力 7 種 × 2 レートで途中につまみを全部動かして NaN 0。CPU 実時間の 0.22 %。つまみを一気に動かしたときの 5 kHz より上は最大 −82 dBFS（RES）
- 59 DUAL FILTER = Doppel-Filter。記録 cap/mod/DoppelFilter 74 通り（ピンクノイズ −20 dBFS・無音区間・DI。scratchpad/dp の dpiso.py・dpglobal.py・dpcmp.py）
  - 作り: A・B 2 つの 2 次の状態変数フィルター（双一次。0.10.0 の検査のあと全体を 4 倍の中で）。ROUTING 0 = 直列（B は A の出口）、1〜3 = 並列（モノの入口では 3 つとも 1 ビットも同じ）。
    MIX は A の出口 ↔ B の出口の等パワー（MIX 0.5 の記録 = 0.707 A ＋ 0.707 A→B にぴったり）。素通しはない。本物は全体が 8 サンプル遅れる（こちらは遅れなし）
  - 1 つずつ取り出す（記録 ÷ もう片方、遅れ 8 を戻す）と 2 次でぴったり（差 0.03〜0.1 dB）。ただし割り算では A と B の音量の分け方が決まらないので、
    最後は線形の記録 52 通り × 左右をまとめて当てはめた（差 0.6 dB）: 中心 537 Hz × 2^(11.555 (CUTOFF − 0.5))、SPREAD の表（左 +2.01 / +0.30 / 0 / −0.81 / −2.01 オクターブ、右は逆）、
    Q 0.48 / 2.13 / 2.86 / 3.53 / 13.3（RESONANCE 0 / .25 / .285 / .315 / .5）、MODE の重み（0 = 0.869 LP、0.5 = 0.918 BP、0.75 = 0.866 BP ＋ 0.439 HP、1 = 0.850 BP ＋ 0.883 HP）
  - RESONANCE 0.75 / 1 は自己発振（本物は入口なしで −22〜−25 / −8〜−11 dBFS、中心の高さ）→ ダンピングを 0.5 から直線で負へ（1 で −0.082）＋ 振れ幅の 2 乗のダンピング（γ 0.39、0.45 より下は 0）。
    こちらは A が 1.5 dB 小さく、B が 3 dB 大きい。本物の RESONANCE 1 は入口と混ざって乱れる（コヒーレンス 0.7）が、こちらはきれいに鳴る
  - DRIVE: −20 dBFS で +1.4 / +3.9 / +5.9 / +7.2 dB → dg × tanh(β x) / β、β = 1 ＋ 1.5 DRIVE（コヒーレンスが本物と同じ 0.95）。FM: そのフィルターの入口で中心を 4 オクターブ × FM × 入口（B の FM がほとんど効かないのも本物どおり）
  - SAFETY 0〜3 は −20 dBFS では 1 ビットも同じ（効きは測れていない）→ 表示だけ
  - 本物と比べた（dpcmp.py）: 74 通り × 左右で差の中央 1.0 dB。外れ: MODE A 0（2.8 dB）、DRIVE 1（2.1〜2.3 dB）、RESONANCE 1（2.5 dB）
  - 自己検査（scratchpad/dp/dftest.cpp）: ブロック差 0、reset 後は新品と同じ、NaN 0（300 組 × 7 入力 × 2 レート）、CPU 0.12 %。ROUTING の切り替えは B の入口を 5 ms でつなぎ替え（一気だと −40 dBFS のプチッ → −88）
    test_inspect の click1 で MODE B 36・MIX 23・CUTOFF B（動かしている間）9.3 → 係数を 16 サンプルごとに作り、間は 1 サンプルずつ直線でつなぐ形に（全部 2.9 以下。RESONANCE A の 98 は自己発振の音そのもので、動かす前から同じ）。
    DRIVE は tanh の音を DRIVE の最初の 5 % で混ぜ入れる（前は DRIVE 0 に戻る瞬間に素通しへ切り替わり、0.5 の音で 0.04 の段）。fuzzall（ASan / UBSan）3 つとも誤り 0
- **DSP 検査 1 回目（0.10.0）「直してから」**（NaN・暴走・確保・ブロック・reset は 3 つとも問題なし。SPEAK FILTER はそのまま渡してよい）→ 直した:
  1. RESO BANK: ナイキストを超える山を 20 kHz（0.45 sr）に止めて重ねていた → STRETCH 1 だけで 20 kHz に +25.8 dB（48 kHz）/ +30.0 dB（44.1 kHz）、ノイズ −12 dBFS で出口 +6.8 / +10.5 dBFS。
     上限・下限（10 Hz）を超えた山は超えた分 1/2 オクターブで大きさを 0 へ → +15.7 dB（本物の +16 dB とほぼ同じ）/ +19.5 dB、ノイズで −1.1 / +1.1 dBFS。合わせ込んだ設定は 1 ビットも変わらない（本物との差 0.33 dB のまま）
  2. DUAL FILTER: DRIVE の tanh・FM が 1 倍で折り返し（4999 Hz −6 dBFS・DRIVE 1・HP で −12.5 dB、BP 初期値で 3 kHz に −27 dB）=「歪み系は 4 倍」に当たる
     → 全体を 4 倍の中で回す（Oversampler4、g = tan(π f / 4 sr)、中心は同じ Hz。遅れ 4 を申告。素通しがないので A・B の位置は自然にそろう）→ 折り返し −113〜−172 dB。本物との差 1.00 → 1.02 dB、CPU 0.64 %（FM・DRIVE 全開 2.2 %）
  3. 出口に 5 Hz の直流カット（直列で A が自己発振し B の FM がそれで揺れると直流 −0.29 → ±0.007）/ 周波数の上限を min(0.45 sr, 22 kHz)（192 kHz で 29.8 kHz の発振 −6.7 dBFS が出た）/
     中の状態の頭打ちを 6 まで線形・8 へやわらかく（硬い ±8 だと大きな入口で角ばった歪み）/ CUTOFF の表示を中心の Hz のまま（digitalHz は 1 倍の表のためのもので、0.95 で 13.9 kHz と出ていた。実際 19.7 kHz）
  4. README: 自己発振は RESONANCE 0.74 から（k が 0 になるのが 0.7398）、大きさはフィルター 1 つで LP −6.7・BP −6.3・HP −3.6 dBFS、A・B とも 1 で −0.1 dBFS
  そのまま: RESO BANK の N(z) と山の形をレートで直していない（6.5 kHz の帯域が 96 kHz で −3.1 dB、192 kHz で −4.0 dB。本物を 96 kHz で測れば決着する）/ RES 1 と STRETCH 1 などを重ねた極端な設定の 20 kHz の山（+41 dB）
- **DSP 検査 2 回目（0.10.0）「直してから」（1 行）**: 4 倍にした DUAL FILTER が遅れ 4 を申告したので、ラックの「遅れのある部品は reset のあと先読みが埋まるまでドライ」に入り、
  再生中の reset でフィルターを通らない音が最大 15 ms 漏れた（LP 200 Hz・ノイズで 2 kHz より上 −69.7 → −18.7 dBFS。0.8.0 の MORPH FILTER と同じ）→ dryAfterReset() = false（−71.2 dBFS）。
  test_inspect の resetleak・swapin に dualflt を足した（空き → DUAL FILTER・MAXIMIZER → DUAL FILTER のプチッ 1.0）。軽い所も直した:
  CUTOFF の上限 18 kHz（上げきって自己発振させると 21.8 kHz で −6.5 dBFS が鳴り続けた。GR の CUTOFF 1 の記録は 23 kHz 付近で、合わせ込みの差がその 1 点だけ広がる）/
  FM の tan を多項式（4 倍の中では 0.36 rad 以下、誤差 1e-5 以下。FM・DRIVE 全開の CPU 48 kHz 2.2 → 1.55 %、192 kHz 8.8 → 6.2 %）/
  RESO BANK の山ごとの消す量を 20 ms でなめらかに（一気に動かすと 10 Hz の下限の消える区間を 10 ms で通り抜け、40 Hz で目安 111 → 消さない版と同じ 20.5）/
  README: OFF の間も中は回るので自己発振が続く・CUTOFF を上げきると 18 kHz が鳴り続ける・20 kHz を超える山は消す
- **DSP 検査 3 回目（0.10.0）「渡してよい」**: reset の漏れなし（ラック全体 LP 200 Hz ノイズ −69.7 → −72.3 dBFS、漏れた長さ 0 ms）、選び直し 7 通り・rack2 のプチッ 1.0、
  RESO BANK の消し方の 40 Hz の目安は消さない版と同じ（20.5 / 19.9 ほか）、CUTOFF 1 の自己発振 18.1 kHz・−6.4 dBFS、FM の多項式の誤差 5.4e-6、ASan / UBSan 誤り 0。
  勧められた 1 か所: 消す量 wvS を flush（1e-42 まで下がるデノーマルを 0 に）
- 60 SPEAK FILTER = Formant Filter。記録 cap/mod/FormantFilter 67 通り（線形・コヒーレンス 1.000。scratchpad/ff の ff3.py・sfcmp.py）
  - 前の「並列 BP で 0.9 dB、形が分からない」の原因: 素通しが「ちょうど 0.5 サンプル遅れ」の対称な FIR（前後に広がる）で、比べ方の窓が前の分を切っていた。
    出口 = (1 − MIX) × 0.5 サンプル遅れの素通し ＋ MIX × 3 本の BP（双一次、山 1）の和。MIX は直線（記録 6 通りで残り 0.003 %）
  - TALK は 0 / .25 / .5 / .75 / 1 の 5 つの母音のあいだを周波数・Q・大きさとも直線で（17 通りで確かめた）。COLOR 1〜12 は 1 ビットも同じ、0 だけ別の組（TALK 0.5 は 0.7 と 0.75 から逆算、TALK 0 は 2 本だけ）
  - SIZE は全部の山を同じ倍率（1 / 1.255 / 1.513 / 1.900 / 2.289、初期値 0.295 で 1.301）。SHARP は Q と大きさの表（3 本の平均。本物は母音によって 3 本目がもっと鋭くなる）
  - こちらの素通しは 1 次のオールパス（a = 1/3、遅れ 0 で大きさは平ら。位相は 16 kHz で最大 22° ずれる）
  - 本物と比べた（sfcmp.py、インパルスの伝達関数 40 Hz〜16 kHz）: 中央 0.46 dB、MIX 0 は 0.00 dB。外れ: SHARP 1 の TALK 0 / 1（1.1〜1.3 dB）、ウェットだけ（MIX 1）0.98 dB
  - 自己検査（scratchpad/ff/sftest.cpp）: ブロック差 0、reset 後は新品と同じ、NaN 0、つまみのプチッ最大 −97 dBFS、CPU 0.08 %

## 次に
- 部品を足す（全部の記録が cap/ にある）: アンプ（Fire Seeker・Jump・ICM の新しいアンプ）とキャビ、歪み、揺れもの、フィルター、ピッチ。足すたびに Registry の後ろへ
- AIO Guitar のエフェクター欄へ移す（本人の将来の希望）
- DUAL DELAY の SYNC、LIMITER の強い抑え続けの差、SPRING の形
