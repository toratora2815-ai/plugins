# FireBox 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

- 1.2.0（2026-11-09）: UNITY の表示名を GAIN MATCH にして、ボタンをプリセット欄の左端に移した（ID unity と DSP はそのまま）。unity ON の間は Gain Stage の LEVEL MATCH を止める
- 1.2.1（2026-11-12）: GAIN MATCH（unity）を、ほかのプラグインと同じ「ON なら IN・OUT をいじっても音量が変わらない」に（健康診断の gainmatch で ON でも +6.6 dB 残っていた）。基準を IN の前の元の音（bypD）にし、IN の分はすぐ戻し（z / gin）、ON の間は OUT を掛けない。IN は歪ませる量だけ変わる。いらなくなった dry2 を外した。OFF の時の音は前と同じ
  DSP 検査係（1 回目「直してから」）: GAIN MATCH ON で IN を動かすと約 50 ms だけ音量が跳ねた（z は latency 前の IN で処理された音なのに、今の IN で割っていた。0 → −12 dB で +11.7 dB）→ IN の値も latency だけ遅らせて割る（ginRing）
