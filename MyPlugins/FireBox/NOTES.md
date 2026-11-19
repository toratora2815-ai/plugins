# FireBox 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

- 1.2.0（2026-11-09）: UNITY の表示名を GAIN MATCH にして、ボタンをプリセット欄の左端に移した（ID unity と DSP はそのまま）。unity ON の間は Gain Stage の LEVEL MATCH を止める
- 1.2.1（2026-11-12）: GAIN MATCH（unity）を、ほかのプラグインと同じ「ON なら IN・OUT をいじっても音量が変わらない」に（健康診断の gainmatch で ON でも +6.6 dB 残っていた）。基準を IN の前の元の音（bypD）にし、IN の分はすぐ戻し（z / gin）、ON の間は OUT を掛けない。IN は歪ませる量だけ変わる。いらなくなった dry2 を外した。OFF の時の音は前と同じ
  DSP 検査係（1 回目「直してから」）: GAIN MATCH ON で IN を動かすと約 50 ms だけ音量が跳ねた（z は latency 前の IN で処理された音なのに、今の IN で割っていた。0 → −12 dB で +11.7 dB）→ IN の値も latency だけ遅らせて割る（ginRing）
- 1.2.2（2026-11-19）: processBlock の頭（入口メーター・作り直し待ちの素通しより前）で、入口の NaN・無限大・+120 dBFS 以上を 0 に（前は NaN 1 サンプルで、GAIN MATCH（unity）の ON・OFF に関係なく出口がずっと NaN だった。DSP 検査係が入っている AU で確認。Shared/GainMatch.h の直しと同じ線）
  同 1.2.2: GAIN MATCH（unity）の 4 秒平均（FireEngine.h の Unity::gain）に、+40 dBFS 以上の入口・出口を入れない（0 として数える）。前は前のプラグインから +40 dBFS 以上が 1 サンプル来ると、その 4 秒は入口の平均だけが大きくなって出口が最大 +23 dB 持ち上がった（DSP 検査係、入っている 1.2.1 で確認。普通の音はビット単位で同じ）
