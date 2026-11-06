# Horizon Spread — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Horizon Spread（HorizonSpread）… ステレオ感を出す手法の全部入り。Mid は触らず Side だけ加工（モノラル互換）。
第1段階 SHAPE ページ: WIDTH 3帯域＋AUTO / AIR / SIDE DRIVE / TRANSIENT、SAFETY: BASS MONO / GUARD、MONO CHECK、ベクトルスコープ。
CREATE ページ（v1.1.0）: SHIFT（SAFE/CLASSIC）/ DOUBLE / BANDS（ALT/TILT）/ HAZE（CLOUD/SMOOTH）/ MOTION。
順番は「生み出す → 整える」で固定。MIX と GUARD の基準は CREATE 前の入力
v1.2.0: AUTO GAIN（標準 ON）、SIDE SOLO、内蔵プリセット11個、帯域ごとの広さメーター、MOTION のテンポ同期、
SHIFT CLASSIC の L/R 選択、DYNAMIC（DYNAMICS タイル）。量のつまみはどのタイルでも右側に置く
v1.3.0: Plugin Link で Fader Pilot がボーカルを上げている間 SPREAD を下げる（ID linkDuck、+6 dB・100% で半分。プリセットで変わらない）
v1.3.1: FROM（相手選び）、Plugin Link v2
