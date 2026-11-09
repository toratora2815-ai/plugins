# Porch Drive — 作業メモ（Claude 用）

- 2026-11-08: 音の処理を Source/PorchDriveDSP.h（namespace pedal）に切り出した。AIO Guitar の EFFECTS も同じものを使う。
  切り出す前と最大差 1.2e-7・遅れ 4 サンプルで同じ音。**ここを変えたら AIO Guitar もビルドし直す**（CMakeLists が ../PorchDrive/Source を include）
- 1.1.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。ON の間は Gain Stage の LEVEL MATCH（linkMatch）を止める
