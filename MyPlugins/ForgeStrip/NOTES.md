# Forge Strip — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Forge Strip（ForgeStrip）… 11 モジュールのチャンネルストリップ。この Mac の定番ストリップ（AU 単体モジュール版）を AVAudioUnit で直接動かして測り、振る舞いを合わせたオリジナル（Source/dsp、JUCE なし、tools/fs_cli.cpp・test_engine.cpp）。
並び替えできるチェーン（apvts.state の "order"）＋最後にリミッター。SCULPTOR（23 楽器の目標カーブ ToneTargets.h、パワーを保って寄せる、STFT 1024）/ EQ 12 帯域 13 形 / COMP×2（検出 = 3.5 ms の最大、直線の倍率で τ = ATT/2・REL/2、戻りは 1/倍率で τ = min(ATT,REL)、ニー幅 = KNEE×1.5）/
EXCITER（XY の 4 角の歪み、4 倍 FIR OS）/ TRANSIENT（3 ms 先読み、tsTune）/ GATE（最低 20 ms 開く）/ DENSITY（34.6 ms 先読み）/ CLIPPER / PHASE（IIR の 90° 対）/ UNMASK（サイドチェーン、帯域の効きやすさは測定の表）。各モジュール最大 3 帯域（LR4）
v1.1.0: コンプ MODE（cmode = Modern / Vintage〔VintageTable.h の 2 次元表、vintTune の 2 段の動き〕/ Punch〔PunchCore、しきい値なし、山にいる間 α(E−B)、下がり始めたら α×16.5/SUSTAIN dB/ms〕）、
自動ゲイン（2 段、最大 20 dB）、自動リリース（長く下げると最大 2 倍）、EQ の Soft Sat（x − 0.24x|x| + 0.036x²）とダイナミック（しきい値→0 dBFS の直線）、
SCULPTOR の Add Punch / Add Polish（PunchPolish）、PHASE は直線位相 FIR（遅れ 2401）、LP/HP はマッチド設計。Plugin Link（独自の種類 200、ForgeLink.h）で Forge Mixer から OUTPUT/PAN/WIDTH を動かす
Forge Mixer 1.0.1: プリセット・取り消しの欄を外した（つまみが LINK だけで、取り消すものがないため。画面は 980×600）
v1.2.0: 差の残りを詰めた。SCULPTOR は帯域ごとの寄せ具合 sculptTune::adapt（平均 0.9 dB）と ToneTargets.h の再校正、Add Punch / Polish は ppTune（山の記憶→持ち上げ、0.5 / 0.9 dB）、
Vintage は vintTune に hold・rc・rp（リリースが R^rp に比例、1.0 dB）、UNMASK は覆い隠しの広がりモデル（unmaskTune::T、相手の各帯域から上へ up0−upK·(差)、下へ down dB/帯域、21 通りで 0.86 dB）。
HP の 20〜50 Hz（基準の −55 dB 付近のくせ）とエキサイター (1,1)（基準は音量で形が変わる）は合わせていない
v1.2.1: 並び順を apvts.state の "order" から、非オートメーションの int パラメーター <pfx>_pos（0〜10、何番目か）11 個に移した。getOrder は pos で並べ替え（同じなら元の順）なので途中の値でも必ずそろう。
これでプリセット・UNDO に並び順が入る。古い保存（order の印だけ）は setStateInformation で pos に直して読む（Swift で確認済み）
同 1.2.1: DSP 検査係の指摘で、ToneTargets.h を 1/12 オクターブの dB 平均＋なめらかさの罰則で当てはめ直した（1.2.0 は 6.4k/8k がジグザグの過学習だった。1/12 で 1.39 → 0.33 dB）。SCULPTOR は基準どおり高域を素通し（SpectralBase::topHz 15600 ± topHalf 1000、全体パワーの計算にも同じ重み）。
Vintage リリースは 1000 ms から先を √ で伸ばす。Spectral.h のフレームごとの std::vector をなくした。holdLeft を reset で 0 に
2026-11-06: Forge Mixer（Frgm）を削除。Strip 側の受け取り（PluginProcessor.cpp の kindMixer、LINK 欄の「Forge Mixer を待っています」）はソースに残したまま（次に Strip を直すときに一緒に片付ける）
- 1.3.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端（ストリップ全体用。モジュールごとの autogain とは別）。BYPASS の間は効かない
- 1.3.1（2026-11-12）: オートメーションのジジッを直した（健康診断の click で 7 個、+52〜+77）。Strip.h: INPUT・OUTPUT・PAN・WIDTH（MONO も）・モジュールの MIX を 20 ms、DELAY L / R を 50 ms で 1 サンプルずつ（DELAY は小数の遅れ。目標は前と同じ四捨五入なので、止まっていれば前とビット単位で同じ）。Dynamics.h: COMP の帯域の GAIN・MIX と全体の GAIN を 20 ms
  DSP 検査係（1 回目「直してから」）: DELAY が float の丸めで目標の整数に着かず、直線補間で高域が落ちたままだった（48 kHz・99 ms で 15 kHz −4.8 dB）→ double で 25 ms × 2 段で追う。MIX 100% の間もドライの遅れ線にいつも入れる（前から: MIX を下げた瞬間に古い音・空の線が出た。DENSITY で +39 dB）
