# Vox Chop — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

Vox Chop（VoxChop）… 声を変えて切って鍵盤で弾く AU 音源（aumu **Vxch**）。2026-11-09 本人「ボイス系を全部集めたプラグイン」→ ミックスエンジニア係の判定「形を変えれば価値あり」→ 本人が A 案を選んだ。
A 案 = AU 音源 1 つ。声の加工は「後から丸ごと計算」だけ（トラックに挿してその場で鳴らす aufx 版は作らない）。
リバーブ・ディレイ・歪み・ボコーダー・ケロケロ・テープストップは入れない（Meld Verb / Echo Drift / Fire Box / EVOC 20 / Pitch Correction / Remix FX）。
Sample Sculptor との分け方: Sample Sculptor =「1 つの音を整える」、Vox Chop =「声を変える・声を切って鍵盤で弾く」。

## 仕組み
- Source/VoxEngine.h（JUCE なし、tools/EngineTest.cpp で数値テスト）
  - analyse: 12 kHz に間引いて YIN（窓 40 ms、5 ms ごと、差分関数は FFT、しきい値 0.15、0.3 より上は声なし）→ オクターブの取り違え直し（±12 フレームの真ん中と 2 倍/半分）
    → 5 点の真ん中 → 3 フレーム以下のすき間はつなぐ・20 ms 未満の声は捨てる。山の位置 = 1 kHz ローパス波形の周期ごとの最大（向きは最初の周期で決める、予想 ±T/4 を探す）
  - GrainPlayer（TD-PSOLA）: 次の粒の「出口での中心」を先に決める（plan）。声 = 一番近い山を中心に ±その場の山の間隔、間隔 = 山の間隔 / 高さの比、
    粒の中を読む速さ = 響きの比（formantRate）。声の出だしは中心を元の山の時刻にそろえる（そのままならほぼ元どおり、誤差 −51 dB）。
    息・子音 = 5 ms の粒を 50% 重ね（ブザーにしない）。音量は sqrt(間隔/粒の半分)。鍵盤では reset (pos, false) で鳴り出しを遅らせない
  - renderVoice: 高さ（shift）× ROBOT（元の真ん中の高さへ対数で寄せる）、体 = formant 半音、GRIT = 周期の長短交互 ±20%・音量交互 −65%・ジッター 10%・シマー 35%（半分の高さのうなり）、
    ALIEN = 体をさらに ±8 半音・ROBOT +0.6・BREATH +0.3・1.3 ms のコム（FB 0.45）、BREATH = STFT（約 21 ms・1/4 ずつ）でパワーを ±250 Hz ならし × 500 Hz の 4 次ハイパスの形 × でたらめな位相（sqrt の前に max(0)、混ぜた後に sanitize）、
    体を上げる（fr > 1.02）ときは先に 4 次ローパス 0.7·fs/(2·fr)（折り返し防止）、体を下げたら 2.5 kHz の棚で最大 +6 dB、
    40 ms ごとに元の声の音量へ合わせる（K 特性のおおよそ = 60 Hz HP＋1.68 kHz +4 dB で比べる、0.3〜(3＋3×BREATH) 倍）、LOW/HIGH CUT（2 次）、0.98 を超えたら全体を下げる。
    作り直した後の山の位置（ctl.marks）を Rendered.an に入れる（鍵盤の PSOLA 用）。BREATH 0.9 以上は声として扱わない
  - GrainPlayer::setLoop（読む位置と粒の予定を範囲内で回す。LOOP の声は Source を声全体・フェードなし）、setLevelFollow（鍵盤 KEEP ON で高さを変えたとき、元の声の同じ位置と 30 ms でならした音量比 0.5〜2 倍）、粒は最大 64
  - analyse / renderVoice / whisperize は cancel（atomic）で途中でやめられる（プラグインを外すとき）
  - autoChop: 5 ms ごとの音量（20 ms 窓）、鳴っている所 = いちばん大きい所 −38 dB 以上、SENS で すき間の最小 220→40 ms・くぼみの深さ 15→4 dB・最短 280→70 ms。
    64 片を超えたら SENS を 0.1 ずつ下げてやり直す。つながっている切れ目は ±3 ms のゼロクロスへ、離れている所は前 10 ms・後ろ 30 ms 残す
  - gridChop: 拍の線（phase）から step ごと、ほぼ無音（ピーク 0.003 未満）は飛ばす
- PluginProcessor: 裏のスレッド（40 ms ごと）で CAPTURE の取り込み・ファイルの読み込み・切り分け・計算し直し（声のつまみが止まって 120 ms）。
  オーディオは tryLock が取れたときだけ手元の audioVoice / audioSlices（shared_ptr）を新しくする。取れなくても前のもので鳴らし、MIDI は捨てない。
  各ボイスは鳴らし始めた声を Voice::buf で持つ（途中で声が入れ替わっても範囲外を読まない）。オーディオで手放した shared_ptr は Graveyard（64 枠）へ入れ、裏のスレッドで捨てる。
  読み込みは LoadRequest（ファイル・切れ目・拍の位置・restore）1 つで受け渡す。state の file / srcPpq / srcBpm は curFile などから getStateInformation で書く。
  外すとき: cancelWork → stopThread(-1)。Bounce・Freeze（isNonRealtime）は processBlock で裏の仕事を最大 30 秒待つ。processBlockBypassed は無音（サイドチェーンを出さない）
  - 鍵盤: 鳴っている声（リリース中も含む、6 ms で消しているものは除く）16、枠 20。同じ鍵盤と CHOKE は 6 ms で止める。17 個目はリリース中でいちばん小さい声（なければいちばん古い声）を 6 ms で消し、空き枠で鳴らす。バイパス・reset() で全部の声を消す。保存から戻すファイルが見つからないときは、その依頼（unresolved）をそのまま保存し直す。FORMANT KEEP ON = 長さそのまま高さだけ、OFF = 早回し。
    LOOP は鍵盤を離すと RELEASE で消える（ONE SHOT より優先）。片の端は Source::read で 2 ms 絞る。最後まで鳴ったら 6 ms で消す。出口は |s| > 8 で 0（安全弁）
  - SLICES = C1（36）から 1 片ずつ、MELODIC = 選んだ片を全部の鍵盤に（C3 = そのまま）、WHOLE = 全体（C3 = そのまま）
  - CAPTURE: 入力バス "Sidechain"（aumu に入力バス。Logic のプラグイン画面の上の Side Chain で歌のトラックを選ぶ）。
    ARM → 再生が始まったら記録（最大 300 秒、モノラル）→ 止めたら裏で −50 dB の所から前 50 ms・後ろ 300 ms で切り、~/Music/Vox Chop/Captures に WAV、
    頭の拍（startPpq = 再生を始めた ppq + 切った分）とテンポを state の "srcPpq" / "srcBpm" に
  - MIDI 書き出し: 頭のある小節の頭を 0 に（4/4 として）、テンポ・拍子入り、960 tick。その小節に置けば元の位置とそろう
  - 書き出しは ~/Music/Vox Chop/<声の名前>/ に毎回新しい名前（getNonexistentSibling）。片はピッチ 0 ならそのまま切り出し、それ以外は GrainPlayer でオフライン
- パラメーター（版のヒント 1）: mode / chopBy / sens / grid / keyMode（オートメーションなし、プリセット・取り消しの対象外）、
  shift / formant / breath / grit / robot / alien / lowcut / highcut（変えると全体を計算し直す。オートメーションなし）、
  keep / attack / decay / sustain / release / velocity / level、linkGroup
- state: "file"、"srcPpq"、"srcBpm"、"manual"、"selected"、子 "SLICES" の "S"（a 始め, b 終わり, p 半音, g dB, f = 1 REVERSE | 2 LOOP | 4 ONE SHOT | 8 CHOKE）。
  開き直したら保存した切れ目をそのまま使う（restoring の間は切り直さない）
- VOICE MAP: 横 = log Hz 50〜800、縦 = 体（半音）−12〜+15。体の目安 bodyOf(f0) = 110 Hz 以下 0、230 Hz 以上 +3（その間は log で直線）。
  目印 MAN 115/0、WOMAN 215/+3、NEUTRAL 165/+1.5、CHILD 290/+5、GIANT 70/−6、TINY 380/+8（絶対の位置。元の声から shift と formant を出す）
- Plugin Link: LINK 欄だけ。種類番号 18 は PluginLink.h の Kind にまだ足していない（AIO Guitar の 17 と同じ。次に Shared を触るときに kindVoxChop = 18）
- GAIN MATCH なし（入力のない音源の扱い。サイドチェーンは取り込み用で、音は通さない）

## 版
- 1.0.0（2026-11-09）: 最初の版。VOICE MAP・BREATH・GRIT・ROBOT・ALIEN・LOW/HIGH CUT・声のプリセット 23 個、AUTO / GRID / 手で切る、片ごとの PITCH・GAIN・REVERSE・LOOP・ONE SHOT・CHOKE、
  SLICES / MELODIC / WHOLE、FORMANT KEEP、ADSR、書き出し（片・全部・MIDI・全体）、CAPTURE

## 後回し（ミックスエンジニア係の線引き）
TREMBLE（老人の震え）、STACK（重ね・コーラス隊・1 オクターブ下）、SNAP（曲のキーへ）、PITCH ENV、スペクトルフリーズ、グラニュラー、スタッターの自動生成、トークボックスふう、レゾネーター、
トラックに挿す版（aufx）は 1.0 を使ってみて本当に要るとわかってから
