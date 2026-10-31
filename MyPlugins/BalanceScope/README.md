# Balance Scope

ミックス・マスターの周波数バランスが「理想的な右肩下がり」の範囲に収まっているかを確認する解析プラグイン（VST3 / AU / スタンドアロン）。
音は一切変えずにそのまま通します。マスタートラックの最後に挿してください。

## 画面の見方
- 白い線（AVERAGE）… 再生している間だけ解析した平均。止めている間は加算されません
- 水色の細い線（LIVE）… 直近1.5秒くらいの状態
- 青い帯（TARGET）… 目標範囲。白い線がこの中に収まっていればバランス良好
- 右の4本のメーター … LOW / LOW-MID / HIGH-MID / HIGH の大まかなズレ。緑なら範囲内、オレンジならはみ出し
- RESET … 平均をリセット（曲や区間を変えたときに）
- 右上のメニュー … 目標カーブのジャンル切り替え

## 使い方
1. マスターに挿す → RESET
2. 確認したい区間（サビなど）を再生
3. 白い線とメーターを見て、はみ出している帯域をEQで調整
4. 参考曲を同じ手順で測って比べるのも有効

## 目標カーブについて
ピンクノイズが平らに見える表示（1/6オクターブ帯域のエネルギー）で、1kHzを基準に右肩下がりの傾きを設定しています。
- Modern (Pop / Rock) … -1.5 dB/oct
- Bass-Heavy (Hip-Hop / EDM) … -2.0 dB/oct ＋ 120Hz以下を +3dB
- Acoustic / Jazz … -1.2 dB/oct、低域控えめ
- Cinematic / Orchestral … -2.5 dB/oct
数値は一般的な市販曲の傾向をもとにした目安で、iZotope製品のデータではありません。
Source/Targets.h の数値を書き換えると自分好みのカーブにできます。
全体の音量差は自動で取り除くので、音量に関係なく形だけを比べられます。

## ビルド方法
必要なもの：CMake 3.22以上、Git、コンパイラ（Mac: Xcode / Windows: Visual Studio 2022）。JUCEは自動ダウンロード。

Mac
```
cd BalanceScope
cmake -B build -G Xcode
cmake --build build --config Release
```
Windows
```
cd BalanceScope
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
