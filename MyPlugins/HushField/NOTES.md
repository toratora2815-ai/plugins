# Hush Field — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Hush Field（HushField）… 動的レゾナンス抑制。市販の定番（Mac に入っている本物）を AVAudioUnit で直接動かして測り、振る舞いを合わせたオリジナル。
HushEngine.h（JUCE なし、vDSP の FFT）: 4096 点を先読み解析 → posToHz の目盛り 1024 点で fine/coarse の山なみ（3 回の箱ならし）、+emph dB/oct で暗めの音を基準 →
soft/hard・DEPTH・DETAIL の量 → 帯ごとの ATTACK/RELEASE（低/高の TILT 倍率）→ LINK・MAX CUT → 最小位相 FIR（LINEAR PHASE は直線位相）をホップごとに作り FFT でたたみこみ・クロスフェード。
遅延 2304（LINEAR PHASE 3456、LOW LATENCY 0＝直接たたみこみ）。帯域カーブ 8 本（ID b1〜b8 の used/on/shape/freq/q/gain/slope/focus）、STEREO L/R・M/S、SC、A/B、プリセット 8 個。
合わせ込みの数値は Tuning（tools/hf_cli.cpp が測定用の CLI）
v1.1.0: Plugin Link（Balance Scope の出すぎ帯域を linkBands で足す、削り量を 16 帯域で送る）、プリセット保存・取り消し
