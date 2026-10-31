# Wool Fuzz

ビッグマフ系の太いファズ（VST3 / AU / スタンドアロン）。

## つまみ
- SUSTAIN … 歪みの量（+6〜+51 dB のドライブ）
- TONE … 0でこもった太い音、10で明るい音。真ん中あたりで中域がへこむ“あの感じ”
- VOLUME … 出力音量（-36〜+6 dB）

## 中身
入力ローカット → 2段の非対称ソフトクリップ（4倍オーバーサンプリング）→ LP/HPブレンドのトーン回路 → 出力

## ビルド方法

必要なもの：CMake 3.22以上、Git、コンパイラ（Mac: Xcode / Windows: Visual Studio 2022）
JUCEは初回ビルド時に自動でダウンロードされます。

### Mac（Logic なら AU、Cubase / Studio One なら VST3）
```
cd WoolFuzz
cmake -B build -G Xcode
cmake --build build --config Release
```

### Windows（Cubase / Studio One）
```
cd WoolFuzz
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

ビルド後、プラグインは自動でシステムのプラグインフォルダにコピーされます
（Mac: ~/Library/Audio/Plug-Ins/、Windows: C:\Program Files\Common Files\VST3 ※管理者権限が必要な場合あり）。
DAWを再起動してプラグインを再スキャンしてください。
Logicで見えない場合は、Logicの「プラグインマネージャ」で再スキャンしてください。
