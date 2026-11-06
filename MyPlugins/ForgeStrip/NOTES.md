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
