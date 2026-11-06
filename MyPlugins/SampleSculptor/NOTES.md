# Sample Sculptor — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Sample Sculptor（SampleSculptor）… サンプル編集プラグイン（AU 音源 aumu Smsc）。ファイルを落とす → 裏のスレッドで丸ごと作り直す（Render.h、JUCE なし、tools/test_render.cpp）→ 試し聴き（つまみを離すと自動、鍵盤 C3 基準でも）→ 箱をドラッグで Logic へ（毎回 ~/Music/Sample Sculptor に新しい WAV）。
順番: START/END 切り取り → PITCH（速さごと、エルミート補間、上げるときは FIR で折り返し防止）→ 形（HitEngine.h = 元 Hit Sculptor の ATTACK/SNAP/LENGTH/ROOM の DRY 側）
→ REVERSE SOUND → ROOM の WET（hs::Room、尾の分だけ長く）→ REVERSE SOUND+ROOM（吸い込む響き）→ DRIVE（4 倍、FIR）→ FADE IN/OUT → NORMALIZE(-0.3 dB)+GAIN。apvts.state の "file" に元ファイルの場所
v1.1.0: Splice / Finder から落とす、EXPORT ボタン、RESULT の波形をつかんで Logic へ（WaveView::onDragOut）、同じ version なら同じファイルを使い回す（lastExportVersion）
v1.2.0: A/B（abMode、A = 元の START〜END を できた音の RMS にそろえて鳴らす matchGain）、STRETCH（timeStretch = WSOLA、窓 2048・±512 を 1/4 に間引いて探す、切り取りの直後）、
TONE: LOW CUT / HIGH CUT（バターワース 4 次、前からだけ）・WIDTH（M/S、DRIVE の後）
（2026-11-06: その場で加工する Hit Sculptor と、逆再生の Flipside を作ったが、本人の意図は「サンプル編集」だったので、これにまとめて 2 つは消した）
