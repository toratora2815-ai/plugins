# Fader Pilot — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Fader Pilot（FaderPilot）… ボーカルの音量を自動でフェーダーを上げ下げして揃える（遅延ゼロ）。TARGET / FLOOR / RANGE / SPEED / OUTPUT、
AUTO RIDE（OFF で Ride パラメーター＝オートメーションをそのまま使う）、WRITE（再生中に Ride へ書き込み、Logic の Latch/Touch で記録）、
FOLLOW MUSIC（サイドチェーンのオケの音量＋OFFSET を目標に）、プリセット8個
v1.1.0: Plugin Link でフェーダー位置（rideDb）と声が鳴っているか（active）を送る
v1.2.0: FOLLOW MUSIC で、サイドチェーンに音がなければ同じ LINK の Balance Scope の musicDb を使う（RiderEngine::setLinkMusic）
