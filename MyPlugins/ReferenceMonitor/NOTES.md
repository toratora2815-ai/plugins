# Reference Monitor — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Reference Monitor（ReferenceMonitor）… 聴き比べ用の単体 Mac アプリ（macOS 14.2 以降、プロセスタップで Logic とミュージックの音を受け取る）。
v1.1.0: LISTEN AS（PHONE / LAPTOP / EARBUDS / BT SPEAKER / CAR / CLUB の聞こえ方、ピンクノイズで音量を自動補正）、
LEVEL に SPOTIFY / APPLE / YOUTUBE の音量正規化、メーターに各サービスで何 dB 上げ下げされるか
v1.2.0: 幅 720〜（MacBook 画面の半分）に収まる縦並びレイアウト。左の参照曲リストは上の REF メニューに
v1.3.3: ウィンドウの位置と大きさを覚える（library.json の settings "window"、動かして 0.8 秒後と終了時に保存）
v1.4.0: グラフを TONE / WIDTH で切替（W キー、WIDTH＝低・中・高のサイドの割合を MIX と REF で比較。Spectrum.h がサイドも測る）、SNAP（N キー、今のミックスを覚えて点線で比較。右クリックで消す）、VOLUME と REF TRIM は上下ドラッグ／ホイールの数字の箱にして、STEREO と一緒に出力の行へ（下は3段）。リモート命令 "width" / "snap" / "snapClear"
v1.4.2: + CUE は名前を聞かずにすぐ追加（名前は空、表示は「CUE 番号」。前の版の「Cue 3」も番号扱い）。キューのボタンをダブルクリックで名前変更
v1.5.0: PUNCH 表示（Punch.h、帯域ごとの 3 秒クレスト dB）、AVG（何曲かの平均の音色と比べる。settings "avgCurves"、ファイルは曲全体を解析、ライブは 15 秒以上の平均）、LISTEN AS と SPOTIFY/APPLE/YOUTUBE は MORE メニューへ（LEVEL は OFF/MATCH だけ、配信サービスのメーター行はそのとき以外隠す）、下は2段で幅は固定、Apple Music の曲は流れた所のモーメンタリー LUFS で音量の形を描く
v1.6.0: LOW 表示（20〜250 Hz を拡大。LowEnd.h の LowAnalyser が 400 Hz で切って約 6 kHz に間引き、4096 点 FFT で約 1.5 Hz きざみ。自分の曲は 100 Hz〜8 kHz の音量差で REF にそろえて描く。SUB / BASS / LOW-MID の差、30〜150 Hz の一番大きい所、120 Hz 以下のサイドの割合）。W キーは 4 つを順番に
