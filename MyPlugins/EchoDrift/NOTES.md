# Echo Drift — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。版を上げたら、ここに 1 行足す。

Echo Drift（EchoDrift、PLUGIN_CODE Ecdr、Plugin Link Kind 15）… テープ・BBD・ラジオ・コーラス系 31 スタイルのディレイ。
この Mac の市販の定番ディレイ（aufx "EB  " SToy、PACE 保護）を測って合わせ込んだオリジナル。スタイルの並びは参考と同じ番号（名前は別）。

## 参考の測り方（build/EchoFit）
- AVAudioUnit を in-process（options: []）で開く。parameterTree は空なので v2 API（AudioUnitSetParameter）で入れる。
  値は「選択肢は番号そのまま、それ以外は 0..1 の正規化」。**つまみを入れたあと RunLoop.main を回さないとスタイルが切り替わらず無音になる**（host.swift の SPIN）
- 時間のつまみ: ms = 0.100279 × (e^(11.9149 x) − 1)。表示より約 3.2 ms 早く返る（遅れ報告 32 サンプルを含めた位置）
- FEEDBACK 0..1.25 は直線（1.0 で低域のループ利得 0 dB）。MIX は等パワー。LOW CUT は出口、HIGH CUT はループの中
- 左右は別々のループ（片側に入れても反対側に出ない）。奇数回目と偶数回目でずれが交互（Digital: 左 −4 / +6 ms、右 +4 / −6 ms 相当）
- GROOVE = 奇数回目を T × g ずらす、FEEL = 全部を 1 拍 × f ずらす（±0.25）
- Ping-Pong: (L+R) が −3 dB、左 = ECHO 1、右 = ECHO 1 + ECHO 2、1 周で FEEDBACK 1 回。Dual: 左 ECHO 1・右 ECHO 2
- Rhythm: タップ k×RHYTHM（k = 1..REPEATS−1）、パターン全体が 2 拍ごとに FEEDBACK でくり返す。音量は √(2/Σg²)（g = DECAY を直線 dB で割り振り）
- FEEDBACK 125% の天井は約 +6 dBFS（テープ系は歪みで 0 dBFS 付近）
- 時間を変えると、読み出しは時定数 0.504 s の指数で新しい時間へ（全スタイル同じ）
- 鳴らし始めの癖は無い（Studio Tape だけ出たのは、最初の時間 250.0 ms から 251.3 ms への 0.5 s の移動を測っていただけ）
- エコーの時間を 40〜1500 ms で変えても、音色・ループの削れ方は変わらない（測って確認）
- ノイズ（ヒス）は出していない

## エンジン（Source/EchoEngine.h、JUCE なし）
- 左右別ループ。線 A → P → 線 B → P → 線 A（× FEEDBACK）。出口は A（奇数回目）と B（偶数回目）の途中から読む → ループ長を変えずに交互のずれ
- P = エンファシス（180 Hz LS / 900 Hz HS / 3.5 kHz HS ＋全体。後で逆の dB で戻す）→ 4 倍 OS（FIR ハーフバンド 95 + 19 点。遅れ 50.5 サンプルは読み出し位置から引く）の歪み
  〔ピークのコンプ（スタイルごとの attack / release、6 dB ニー）→ (f(x+b)−f(b))/f'(b)、f = x/(1+|x|^k)^(1/k)〕→ 戻し → ループのフィルター → split（A に +½、B に −½）
  → HIGH CUT（1 次 LP、24000·e^(−1.22v) Hz）→ 天井（1.5 から tanh で 2.0 まで）
- 参考は奇数回目（A）と偶数回目（B）で通るフィルターが違う（音量に関係なく交互）。split = A−B の音色差（LS/ピーク/HS）。RBJ の棚・ピークは逆の dB で打ち消すので A×B（2 回分）は変わらない。
  1 回目は A を通るので、出口で −½ を掛けて戻す
- SATURATION はスタイルごとに 0/6/12/18/24 dB での「入れる dB・戻す dB」の表（間は直線）。直線の式では合わなかった（12→24 dB で急に変わる）
- 出口 = 色のフィルター（1 回目のエコー ÷ ループ 1 回分）→ split の −½ → にじみ（左右別のオールパス 4 段、係数と長さはスタイルごと）→ LOW CUT（1 次 HP 2 段、表 v = 0/0.1/0.25/0.5/0.75/1）
- にじみはループの外で 1 回だけ（参考は 2 回目のエコーも 1 回目と同じ広がり）。広がりは時間に関係なく一定で、読み出しを前に出して（例 Diffused −53 ms）中心を合わせる。
  Splattered だけは約 213 ms より短くならない（minMs）
- 読み出しは 32 点の窓付き sinc（Kaiser β 9、512 位相）。最小の遅れ 17 サンプル
- 揺れ（ModGen）: 音程（セント）= 正弦 ＋ 四角（＝読み出しは三角に動く。半周期・ばらつき・角のなまり、右は同じ / 逆 / 別々）＋ ランダムなフラッター。
  位置のずれ = ∫(1 − 2^(c/1200))（30 秒で 0 に戻る漏れ）。0 を中心に動くように、四角は最初を半分の長さ、正弦は位相 0.25 から
- 時間: ms のつまみはなめらかに追う（時定数 0.504 s の指数、全スタイル同じ）。音符・テンポで決まる時間は 20 ms のクロスフェードですぐ切替（TimeState）

## スタイル表（Source/Profiles.h、自動生成）
build/EchoFit で: m1/m3（周波数特性・ループ）→ fit1.py（RBJ の直列に当てはめ）、m6/m6b（歪み）→ fitsat2.py、m5（揺れ。手で表にした値は gen.py の MOD）、
diffq.json（にじみの形、左右別 5 点）→ fitdiff2.py、m9（時間ごと・奇数偶数）→ split.json、m11（周波数ごとの歪み）→ fitemph.py → emph2.json、
m12（コンプの速さ）→ fitcomp.py、calib3.py（ずれ・音量・残りの A−B を 100 Hz〜8 kHz のエネルギーで合わせ込み、T = 700 ms）→ gen.py が Profiles.h を書く。
host.swift / render_echo は環境変数 CHANGE="秒:id=値" で途中のつまみ変更、render_echo は ATT / REL / WOBBLE で上書き。
比較: cmp1.py（時刻・帯域ごと）、cmpsat.py（歪み）、cmpmod.py（揺れ）、m9/m10/m11/m12 の mine。
結果（1.2.0）: 短い音の頭 平均 1.9 dB（最大 8.6）、2 音の混変調 平均 4.0 dB、FEEDBACK 125% の行き着く音量 平均 2.9 dB・山 4.1 dB（テープ系は山が 3〜5 dB 尖る。
Shortwave / Tube Reel の SATURATION 12 dB、Diffused、Squawk〔参考は 125% でも消える〕は合っていない）。Ping-Pong・Rhythm は普通のスタイルで ±0.2 dB・±1〜3 ms。
結果（1.1.0）: 10 kHz までほぼ ±1〜2 dB、奇数偶数の音色差 平均 0.8 dB、周波数ごとの歪み 平均 0.8 dB、コンプの動き 平均 1.8 dB、揺れの深さ ±10%。

## ツール
- tools/render_echo.cpp … 参考と同じパラメーター番号で WAV を通す（WOBBLE=0 の環境変数で揺れなし）
- tools/test_echo.cpp … 数値テスト（時刻・音量・FEEDBACK・MIX・125% で発振しない・無音・Ping-Pong・44.1/96 kHz・揺れ・処理の重さ）

## 版
- 1.2.0（2026-11-06）: 音符・テンポで決まる時間は追いかけず 20 ms のクロスフェードで切替（参考と同じ。ms のつまみだけ 0.504 s で追う）。
  Ping-Pong は 1 往復で処理 1 回（右へ渡る所は素通し）、にじみスタイルは往復ごとに広がる（ループの中、Verbed だけは出口）。
  Rhythm は左右を足して両方へ同じものを出す、音量 = g/√(2Σg²)、RHYTHM の時間の式 0.1003·(e^(12.61x)−1)。
  揺れは「B から A へ戻る所」と出口のタップだけ（2 回目は 1 回目と同じ深さ、3 回目で T 離れた 2 つが重なる）、Vibrato は出口で 1 回だけ（modOut）。
  フラッターは帯域（flRate/4〜flRate）、右が左を遅れて追う型（sqRMode 3、Digital Chorus 0.6 s）、Roller Echo などの揺れの表を帯域ごとに合わせ直し。
  歪み・コンプを 15 スタイルで当て直し（fitjoint.py: 正弦の音量・倍音、コンプの動き、2 音の混変調、短い音の頭を同時に、エンジンを鳴らして Nelder-Mead）。
  FEEDBACK 100% 超えの天井をスタイルごとに（ceilDb、0〜8 dB。ふつうの音に触れないよう 0 dB 以上）
- 1.1.0（2026-11-06）: 奇数・偶数回目の音色差（split）、歪みの周波数ごとの効き方（エンファシス）とコンプの速さをスタイルごとに、
  にじみをループの外へ（左右別の形、時間に関係なく一定、Splattered の最短 213 ms）、時間を変えたときの動きを 0.504 s に
- 1.0.0（2026-11-06）: 最初の版。31 スタイル、4 モード、テンポ同期、PRIME、WOBBLE、内蔵プリセット 12、Plugin Link（Fader Pilot → VOCAL DUCK）
- 1.3.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。MIX 99.5% 以上は効かない。applyPreset の初期化から gainMatch を外した。下の欄を 2 段（footerH 72）に
- 1.3.1（2026-11-12）: オートメーションのジジッを直した（健康診断の click で Feel +67・Groove +64・Output +52・Mix +42）。INPUT / OUTPUT / MIX は 20 ms、FEEL・GROOVE（エコーを読む位置）は TIME と同じ 0.504 s で 1 サンプルずつ追う（EchoEngine.h の follow / snapSmooth）。止まっている時の音は前とビット単位で同じ
  DSP 検査係: 追う式を「丸めでもう進まないなら目標ちょうど」に（192 kHz で目標の直前で止まり、係数を作り直し続ける所があった）
- 1.3.2（2026-11-18）: FEEDBACK 100% 未満なのに鳴り止まない所を直した（健康診断のランダム設定で見つかった。本物（build/EchoFit/host）と測り比べて決め、DSP 検査係と 3 回やりとり）
  - SATURATION: 処理 P の歪みの小さい音の倍率（satIn + satOut、SAT 24 dB で最大 +12 dB）がくり返すたびに掛かり、SAT 24 dB では FEEDBACK 40・60% の 62 通り中 44 通りが鳴りっぱなし（0 dBFS 近くも）。
    本物は SAT を変えても「くり返し 1 回の減り方」が全スタイルで同じで、1 回目のエコーだけ +6〜12 dB 大きくなる → ループへ戻す量を fb × G0 /（入れる倍率 × 戻す倍率）にした
    （G0 = SAT 0 dB の倍率。P を k 倍・天井も k 倍にして出口で 1/k に戻すのと同じ計算。止まっていれば fbKT ちょうど＝SAT 0 dB は前とビット単位で同じ）
  - 入れる倍率・戻す倍率（Saturator の driveT / makeupT）と FEEDBACK（fbS）は 20 ms で追う（glide / follow、丸めでもう進まないなら目標ちょうど）。
    前は一気に切り替わり、入れる所と戻す所が 4 倍のフィルターの遅れ（約 1 ms）だけずれてプチッと鳴った。FEEDBACK もブロックごとの段だった
  - Ping-Pong・Rhythm は 1 周で pa しか通らないので、奇数回目の差（split・passTilt の +½）が毎周積み上がり、Telephone は 3 kHz で +8.8 dB → FEEDBACK 40% から鳴りっぱなしだった（本物は 60% でも消える）。
    差の量 a をスタイルごとに prepare で計算（onePassAlpha: FEEDBACK 100% のループ利得の最大が、差なし（a = 0）と 0 dB の大きいほうを超えない範囲でいちばん 0.5 に近い値）。
    a < 0.5 は Telephone 0.09・AM Radio 0.21・Bucket Swirl 0.17・Bucket Dark 0・Cheap Cassette 0 だけ。ほかは前と同じ音（全部 0 にすると Ping-Pong の減り方の本物とのずれが 0.29 → 0.43 dB/回に広がった）
  - レート: ループのフィルターは 48 kHz で合わせてあり、ほかのレートでは 20 kHz 近くの形が変わって（0.49·fs に寄せる所も）ループ利得が 1 を超えていた（44.1 kHz の FM Radio は 80% から +10 dBFS、96 / 192 kHz の Vibrato は 60% から）。
    prepare でスタイルごとに、48 kHz のときの最大（と 0 dB の大きいほう）を超える所だけ下げるピーク（Q 0.7）を作る（makeGuard、Pass の guard。48 kHz では何も足さない）
  - Verbed: ループの 30.6 Hz・Q 8.1 のシェルフで 29 Hz が +1.2 dB → FEEDBACK 88% から 29 Hz で鳴りっぱなし（本物は 90% で消える）。40 Hz 未満で 0 dB を 0.1 dB より超える所を Q 6 で削る（addSubBassCut、Pass の guardSub）。
    削るのは FEEDBACK 100% 以下のときだけ（超えたら g = 0）。Q を下げて山をなくす・削ったまま 125% にすると、40〜80 Hz も育って 125% が本物より 9 dB 大きくなった（前は 29 Hz だけが先に天井に当たり、出口で −5.6 dB 削られていた）。
    125% は 1.3.1 とほぼ同じ（SAT 0 で山 +3.5 / 平均 −2.3、本物 +3.2 / −1.2）
  - モードを替えるとき: エコーの音と線に書く音（入力・くり返し・Ping-Pong の右への写し）を 10 ms で下げ切り、15 ms 待ってから替え、10 ms で戻す（set → pendingMode、process の modeG・holdN）。
    前は係数と読み出しが一気に変わり、つなぎ目が 1 周後にプチッと出た（3 kHz 以上の山: 中央 −4 → −52 dB、最大 +23 → −18 dB）。下げた 20 ms は 1 周ごとの小さなへこみとして残る。使っていない線には 0 を書き、使っていない歪みの倍率はそろえ、
    出口のにじみは使わない間も回す、Ping-Pong に入るときはループの中のにじみを空にする（Single に戻したとき前のエコー・響きがまた鳴らないように）
  - Digital Chorus の揺れの記録（hist）を prepare で確保（前は最初に選んだとき音の処理の中で約 115 KB 確保していた）
  - 確認: SAT 0・つまみ固定で 124 通り中 110 通りが前と完全に同じ（違うのは Verbed と上の 5 スタイルの Ping-Pong・Rhythm）。FEEDBACK 40・60% × SAT 0/12/24 は全部消える（本物も）。
    44.1 / 48 / 96 / 192 kHz × 3 モードで FEEDBACK 80% 以下は全部消える。FEEDBACK 125% の行き着く音量の本物とのずれ 537 → 333 dB（Queeked は本物どおり 125% でも消える）。
    tools/test_echo.cpp に鳴り止む試験を 5 つ足した（1.3.1 では 5 つとも FAIL）。道具（使い捨て、コミットしない）: satloop.py・satdecay.py・ppdecay.py・ceilcmp.py・highfb.py・ident.py・verbceil.py・modesw_an.py
  - 残り（前から）: Logic が止めてもエコーが残る（reset を上書きしていない）
- 1.3.3（2026-11-18）: スタイルを鳴らしながら替えたときのプチッを直した（本人「直して」）。モードと同じく、エコーの音と線に書く音を 10 ms で下げ切り、15 ms 待ってから替え、10 ms で戻す
  （set → pending・pendStyle、process の modeG・holdN。ブロックの途中で替えたら styleVals でタップの位置・揺れ・最短の時間を読み直す）。
  - 線に書く音は歪みの前と後の両方で下げる（前だけ: 強い歪み（Distorted は SAT 24 で入れる倍率 +45 dB）が音量を保って書く音が急に切れた。後だけ: 歪みの中のフィルターが響いて悪化）
  - 線の中も入力も −100 dBFS 未満が「いちばん長い読み出し＋FEEL の大きさ＋長いほうの時間 × 0.25（GROOVE）＋0.3 秒」続いていたら、待たずにすぐ替える（quietN / quietNeed。止めている間に替えて再生したとき、1 回目のエコーが欠けないように）
  - スタイルを替えたら歪みの倍率は一気に（snap）。1.3.2 では前のスタイルの倍率から 20 ms で追っていて、替えた直後の音が強く歪んでいた（無音のあとの 1 回目のエコーが最大 +20 dB）
  - 結果（31 通りの組み合わせ、220 Hz −30 dBFS・FEEDBACK 60%、3 kHz 以上の山を切替前の音の山と比べて）: SAT 0 で中央 +18 → −54 dB・最大 +53 → −25 dB、SAT 24 で中央 +17 → −50 dB。
    SAT 24 の Transmitter ⇄ Distorted だけ +5 dB 残るが、一瞬ではなく、Distorted で書かれた前のエコーが Transmitter の歪みを通った音色で鳴り続けているだけ。モード切替も中央 −53〜−55 dB・最大 −27〜−23 dB に
  - DSP 検査係（4 回目）: OK。992 回の切替で中央 +2.8 → −56 dB・最大 +68 → −5 dB。静かなときにすぐ替えた後の 1 回目のエコーは、最初からそのスタイルの場合と ±0.1 dB。
    FEEL・GROOVE の分を「静か」の長さに足したのは検査係の指摘（50 bpm 以下で FEEL を大きく＋にすると、ループが回っているのにすぐ替わることがあった）。
    残り: 下げた 35 ms がループの中に穴として残り 1 周ごとにへこむ（なくすには P を 2 つ並べてクロスフェードが要る。短くするとチッが戻るのでこのまま）。Squawk → Telephone だけ −15〜−20 dB の小さなチック
  - SAT 0・つまみ固定で 1.3.1 と完全に同じなのは前と同じ 110 / 124。道具: stylesw.cpp・stylesw_an.py・quietsw.cpp・quietlogic.cpp（使い捨て）
