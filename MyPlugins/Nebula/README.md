# Nebula — cinematic reverb

映画・アンビエント向けの長く広がるリバーブ（VST3 / AU / スタンドアロン）。

## つまみ
- MIX … 原音と残響のバランス（センドで使うなら100%）
- DECAY … 残響の長さ（1〜30秒）
- SIZE … 空間の大きさ
- PRE-DELAY … 残響が始まるまでの間（0〜250ms）
- DAMPING … 残響の高域の減り方（低いほど暗くなる）
- MOD … 残響の揺らぎ（コーラスのような広がり）
- SHIMMER … 1オクターブ上の響きを残響に重ねる（上げるほど天使っぽく）
- WIDTH … ステレオの広さ（0でモノラル、150%で超ワイド）
- LOW CUT … 残響の低域カット（濁り防止）
- FREEZE … 押している間、今の残響を鳴らし続ける（ドローン作りに）

## おすすめ設定
- 壮大なパッド … DECAY 15s / SIZE 90% / MOD 60% / SHIMMER 40% / MIX 50%
- 映画のインパクト音 … DECAY 6s / PRE-DELAY 80ms / DAMPING 4000Hz / LOW CUT 200Hz
- ピアノのアンビエント … DECAY 10s / SIZE 70% / DAMPING 3000Hz / MOD 30%

## 中身
プリディレイ → 拡散オールパス4段 → 8本のフィードバック・ディレイ・ネットワーク（ハウスホルダー行列、モジュレーション付き）
シマーは残響出力を+12半音ピッチシフトしてネットワークに戻す方式。

## ビルド方法
必要なもの：CMake 3.22以上、Git、コンパイラ（Mac: Xcode / Windows: Visual Studio 2022）。JUCEは自動ダウンロード。

Mac（Logic は AU、Cubase / Studio One は VST3）
```
cd Nebula
cmake -B build -G Xcode
cmake --build build --config Release
```

Windows
```
cd Nebula
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
ビルド後、プラグインフォルダに自動コピーされます。DAWで再スキャンしてください。
