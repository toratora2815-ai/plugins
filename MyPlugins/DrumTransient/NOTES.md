# Drum Transient — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

Drum Transient（DrumTransient、aufx Drtr Mypl）… ドラム専用のトランジェント・リミッター。2026-11-09 本人「unilmasterlimiter（Tone Projects Uni-L、aufx UnL1 ToPr）を
実験して同じものを。普通のリミッターは Pro-L2 で足りているので、ドラムのトランジェント処理に特化して」。ミックスエンジニア係「形を変えれば価値あり（ドラム用に絞る）」→ 本人「ドラム用に絞る」。

## 仕組み（Source/DrumTransientDSP.h、JUCE の Oversampling だけ使う）
PUSH（乗算型 SmoothedValue 30 ms）→ BODY（5 kHz LPF の検出で、遅い包絡 1/250 ms と速い包絡 1/25 ms の差 dB × body、±12 dB）
→ 胴のリミッター: 窓 w+1 の最大値（単調キュー）＋箱型平均 w（先読み 5 ms）。見る量 = ピーク dB − PUNCH × アタック量（速い 0.1/8 ms と遅い 12/120 ms の包絡の差、0〜24 dB）− クリップの肩。
  しきい値は CEILING ＋ CLIP × 12 dB × tr（tr = アタック量/6 dB、下限 0.3）。戻りは 2 段: rs（最大 5 dB、溜まる 60 ms、戻り 40〜400 ms）＋ rf（すぐ下げ、戻り 1〜20 ms）
→ クリッパー（4 倍 FIR 半帯域、肩 = 天井 × (1 − 0.1 × PUNCH)、tanh）。誤差 e を LOW KEEP の 4 次バターワース HPF に通して足す（CLIP×4 で 0〜1 倍）
→ 仕上げリミッター（先読み 1.5 ms、戻り 4〜20 ms）→ 天井で安全クリップ → DELTA（dry×PUSH を総遅れだけ遅らせて引く）
遅れ = 5 ms + OS の遅れ × 2（1.1.0 から 2 回）+ 1.5 ms（48 kHz で 431。1.0.0 は 373）。メーター: meterBody = rs+rf の最大、meterAttack = 胴のリミッター後の天井越えの最大

## ID
push（0〜20 dB、既定 6）/ punch（0〜100、60）/ body（−100〜100、0）/ clip（0〜100、40）/ release（0〜100、50）/ lowKeep（OFF/60/90/120/160、既定 90）/
ceiling（−0.1/−0.3/−1.0、既定 −0.3）/ stereoLink（ON）/ delta / gainMatch（版のヒント 10）/ linkGroup。全部 版のヒント 1。
Plugin Link の Kind は 19（PluginLink.h には未登録。Vox Chop と同じく番号だけ。相手なし、LINK 欄だけ）。LevelMatch（Gain Stage の MATCH）は付けない（出口のあとで上げると天井を越えるため）
GAIN MATCH: limitBoost(0)（下げるだけ）、DELTA の間 amount 0、ignoreIds に delta。内蔵プリセット（applyPreset）は gainMatch・linkGroup 以外を初期値に戻してから入れる

## 合わせ込み（build/DrumLab、コミットしない）
host（AVAudioEngine オフライン、build/fuellab/host.swift と同じ）、lib.py（render(x, tail, _mine=True, **{名前: 正規化値})）、
cmp.py・q2.py（ループ 2 本 × +6/+12/+18 dB とキック・スネア単発の音量差、50 Hz 持続音の歪み）、env.py（2 ms ごとの下げ量の比較）、tnum.py（数値テスト）、inst.sh（手元ビルド→インストール）。
素材は ~/Splice/sounds のドラムループ 2 本・キック・スネア（src/ に 48 kHz で変換）。
Uni-L: Gain 0..1 = 0〜+20 dB、Input 0..1 = −20〜+20 dB、遅れ 2091（44 ms）、出口 0 dBFS。初期設定で「天井を越えた分はその場で下げる（2 ms ごとに見ると出口がずっと 0 dB）、
抜けたあと 4〜6 dB 残して約 0.15 秒で戻す」。200 Hz の持続音を抜いたとき −12 → −7 dB まで数 ms、その後ゆっくり。50 Hz 持続音 +12 dB で歪み 2.3%（遅れ 44 ms の長い先読みのおかげ）。
結果（1.0.0）: ループの聞こえる音量の差は +6/+12/+18 dB で 0.0〜+0.1 / −0.8〜−0.9 / −1.4〜−1.7 dB（こちらが控えめ）、キック単発 −0.3〜−0.5、スネア単発 −1.7〜−3.7 dB。
50 Hz 持続音 +12 dB の歪み 5.8%（LOW KEEP 90）/ 3.9%（160）。クリッパーにアタックの所だけ任せる（tr）で 11.7% → 5.8%、そのぶん大きい押し込みで 1 dB ほど控えめになった。
HOLD（速い戻りを 12 ms 待つ）も試したが、音量が 1〜3 dB 下がるのに歪みはほとんど減らなかったので入れていない

## 版
- 1.0.0（2026-11-09）: 最初の版
- 1.1.0（2026-11-10）: 本人「1・2・4 をやろう」（スネアの音量・LEARN・流れる表示）。ミックスエンジニア係「2・4 は価値あり、1 は低音の割合をゆっくり追う形なら」
  - スネア: 原因は 4 倍で切った音を戻すときの山の行き過ぎ（それを仕上げのリミッターが下げていた。CLIP 100% にしても差は変わらなかった）。
    低音の割合 lowFr（150 Hz の 2 段 LPF の包絡 / 全体の包絡、60 ms → 割合を 300 ms でゆっくり追う）から notLow（0.3〜0.7 で 1→0）。
    notLow の分だけ tr の下限を 1 へ、2 つ目の 4 倍（os2）でもう一度同じ肩で切る（誤差は 4 倍の LOW KEEP HPF hpB に通して notLow × CLIP 量）。
    最初は基本レートで切って +0.2/−1.2/−1.8 まで縮んだが、DSP 検査係「折り返しが −46〜−76 → −21〜−38 dB に増える」で 4 倍に移した。
    os・os2 とも useIntegerLatency=false（true だと Thiran オールパスが 2 段で、並列に混ぜると 48k 20 kHz −7.7 dB。検査係の指摘）。59.5+59.5=119 を足してから丸め、計 431（44.1k 406 / 96k 743）。LEARN 完了は state→busy の順に store。
    繰り返すスネアで Uni-L との差 −1.7/−3.0/−3.7 → −0.7/−1.8/−2.3 dB、ループ ±1.0 以内、キック・50 Hz の歪み 5.8% は変わらず。1 発だけのスネアは割合が追いつかないので変わらない（わざと）。
    折り返し（一定のサイン波 0.9 FS、PUSH 12）: 3.1k −63.7、7.3k −52.0（CLIP 100 −44.6）、11.7k CLIP 100 −35.8 dB。ドラムに近いバーストでは 1.0.0 と同じか少し少ない
  - LEARN: processBlock で入口のピーク -40 dBFS より上のブロックだけ 5 秒ため、裏のスレッド（LearnThread）で別の dt::Engine に PUSH を変えて通し、
    10 ms ごとの meterAttack のヒットの山（±40 ms で最大、0.2 dB 以上）の上位 90% が目標になる PUSH を二分探索。頭打ちが目標より下なら頭打ちの 9 割。
    目標は apvts.state の learnTarget（内蔵プリセットで 4/3/4.5/3/4/4.5/12）。結果は AsyncUpdater で push に入れて gainMatch.requestRelearn。learnBuf は toggleLearn（メッセージスレッド、聞く前）でその時のレートで用意。裏のスレッドは 50 ms ごとの見回り（オーディオスレッドから notify しない）、measure の中でも threadShouldExit を見る
  - 流れる表示: dt::Engine に 5 ms ごとの履歴（histAtk / histBody / histIn、2048 列、atomic）。画面は 1000 幅に、右の 300 px で 3 秒分。BODY の帯の下に ATTACK のトゲを継ぎ足す
  - テスト: build/dtlearn（本物のプロセッサーで LEARN・画面の画像、コミットしない）
