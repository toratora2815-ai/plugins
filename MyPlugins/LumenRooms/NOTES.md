# LumenRooms 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

- 1.5.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。MIX 99.5% 以上は効かない（センド使い）。setCurrentProgram と .vpreset 読み込みの初期化から gainMatch を外した
