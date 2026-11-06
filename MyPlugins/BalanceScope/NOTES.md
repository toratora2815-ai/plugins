# Balance Scope — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Balance Scope（BalanceScope）… 帯域バランス解析。ジャンル別目標カーブ、4帯域メーター、RESET
v1.1.0: 参考曲のカーブを CAPTURE して比べる（REF A/B/C、apvts.state の子 "REFS" に保存、DIFF 表示）、ラウドネス（INTEGRATED/SHORT-TERM/MOMENTARY/PEAK/PLR、LoudnessMeter.h）、Custom の目標カーブ（ID customTilt / customLow）
v1.2.0: Plugin Link で「出すぎの帯域」を送る（BandMath.h の findExcess。平均 3 秒以上、150 Hz〜12 kHz、REF と比べているときは REF 基準）
v1.3.0: Link で上位 2 か所（findExcess2）、JUDGE BY（ID linkWindow、WHOLE / LAST 10 s＝時定数 10 秒の recentPower）、オケの音量 musicDb をブロックごとに送る、
Velvet Tamer（今は Hush Field）の削り量（グラフに青緑の棒）と Gain Stage の GAIN を受けて表示
