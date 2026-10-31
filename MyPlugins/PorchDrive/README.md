# Porch Drive

ブルース系オーバードライブ（VST3 / AU / スタンドアロン）。
BD-2系ペダルのキャラクター（ピッキングへの反応、鈴鳴り感の残るクランチ）を参考にしたオリジナル設計です。

## つまみ
- LEVEL … 出力音量（-30〜+12 dB）
- TONE … 5が素の音。上げると高域が明るく、下げると丸く
- GAIN … 歪みの量。1段目と2段目が同時に上がる

## 歪み方の目安（ギター程度の入力で）
- GAIN 0〜2 … ほぼクリーン〜軽い味付け。ブースター的に
- GAIN 4 前後 … クランチ。弱く弾くときれい、強く弾くと歪む
- GAIN 7〜10 … 太いオーバードライブ

## 中身
入力ローカット → 1段目：非対称ソフトクリップ（温かい偶数倍音）→ 2段目：ダイオード型クリップ
（ここまで4倍オーバーサンプリング）→ 900Hz境目のトーン → 高域のザラつき除去 → 出力

## ビルド方法
必要なもの：CMake 3.22以上、Git、コンパイラ（Mac: Xcode / Windows: Visual Studio 2022）。JUCEは自動ダウンロード。

Mac
```
cd PorchDrive
cmake -B build -G Xcode
cmake --build build --config Release
```
Windows
```
cd PorchDrive
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
ビルド後、プラグインフォルダに自動コピーされます。DAWで再スキャンしてください。
