# Hush Field — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Hush Field（HushField）… 動的レゾナンス抑制。市販の定番（Mac に入っている本物）を AVAudioUnit で直接動かして測り、振る舞いを合わせたオリジナル。
HushEngine.h（JUCE なし、vDSP の FFT）: 4096 点を先読み解析 → posToHz の目盛り 1024 点で fine/coarse の山なみ（3 回の箱ならし）、+emph dB/oct で暗めの音を基準 →
soft/hard・DEPTH・DETAIL の量 → 帯ごとの ATTACK/RELEASE（低/高の TILT 倍率）→ LINK・MAX CUT → 最小位相 FIR（LINEAR PHASE は直線位相）をホップごとに作り FFT でたたみこみ・クロスフェード。
遅延 2304（LINEAR PHASE 3456、LOW LATENCY 0＝直接たたみこみ）。帯域カーブ 8 本（ID b1〜b8 の used/on/shape/freq/q/gain/slope/focus）、STEREO L/R・M/S、SC、A/B、プリセット 8 個。
合わせ込みの数値は Tuning（tools/hf_cli.cpp が測定用の CLI）
v1.1.0: Plugin Link（Balance Scope の出すぎ帯域を linkBands で足す、削り量を 16 帯域で送る）、プリセット保存・取り消し
- 1.2.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。DELTA / SC LISTEN / band solo / BYPASS の間は効かない（その 4 つは動いても測り直さない）。遅れは eng.latencyFor をそのまま渡す。A/B 切替で gainMatch を保つ、applyPreset の初期化から外した。下の欄を 2 段に
- 1.2.1（2026-11-09）: 音のスレッドでメモリを確保しないように。readSettings / applyLink が毎ブロック ID の文字列を作っていた（512 サンプルで new 約 210 回）のを、コンストラクターで取った `std::atomic<float>*`（prm）を読むだけに。最初のブロックの FFT などの準備（configure）も prepareToPlay の preconfigure へ（prepareToPlay の時点とモードが同じときだけ効く）。音は前とビット単位で同じ（build/hfalloc で 20 場面を比べた）。quality / LINEAR PHASE / LOW LATENCY が prepareToPlay の後で変わったブロック（切り替えた瞬間、Initialize の後に状態を戻した曲の頭、プリセット・UNDO・A/B でモードが変わった直後）は、前と同じく作り直しで確保する（直すなら prepare で最大の大きさを確保しておく）
- 1.2.2（2026-11-12）: MIX・WET TRIM・OUT TRIM を 20 ms でなめらかに（健康診断の click で Out Trim +55・Wet Trim +53・Mix +30）。LOW LATENCY のたたみこみ（directConv）を、2 倍の輪（convRing）＋前後を逆にしたフィルター（firRev）の vDSP_dotpr に（1 コアの 15% → 10%、出力の差 −117 dB）。初回の健康診断の「重い設定 44〜53%」は Mac が混んでいたため（空いている時は 15〜17%。LOW LATENCY ＋ QUALITY 高の組み合わせだけ重い）
  DSP 検査係: 追う式を「丸めでもう進まないなら目標ちょうど」に（192 kHz で目標の直前で止まり、係数を作り直し続ける所があった）
