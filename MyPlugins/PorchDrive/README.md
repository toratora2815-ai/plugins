# Porch Drive

ブルース系オーバードライブ（Mac の Logic Pro 用 AU）。
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

## Plugin Link：Gain Stage と音量をそろえる（画面のいちばん下）
自作プラグイン同士が裏で情報をやりとりします。いちばん下の帯の **LINK**（LINK OFF / LINK 1〜4）を、つなげたいプラグイン同士で**同じ番号**にするだけです。最初は OFF。
- 同じトラックの前のほうに **Gain Stage** を挿して同じ LINK にすると、Porch Drive の出口の音量を Gain Stage の **TARGET** にそろえます（約 2 秒でゆっくり、最大 ±18 dB）。
  歪みを深くしても音が大きくならないので、「大きくなったから良く聞こえる」にだまされずに音色だけで比べられます
- LINK 欄の右の **MATCH**（最初は ON）で入り切り。Gain Stage が **RMS** モードのときだけ効きます（PEAK のときや相手がいないときは、ゆっくり 0 dB に戻ります）
- 帯の文の例：`matched to TARGET (-4.2 dB)`。相手が見つからないときは `waiting for Gain Stage (RMS mode)`
- **FROM**（LINK の右）… 同じ LINK に Gain Stage が 2 つ以上いるとき、どれに合わせるか選べます（AUTO = いちばん新しく送ってきた相手。トラック名で覚えます）

## プリセットの保存・取り消し（画面のいちばん下）
- **UNDO / REDO** … つまみの操作を 1 手ずつ取り消し・やり直し（100 手まで。画面を閉じても残ります）
- **< >** … 前／次の自分のプリセット。真ん中の名前を押すと一覧（Save as... / Delete / Show preset folder in Finder）
- **SAVE** … 今のつまみに名前を付けて保存（~/Library/Audio/Presets/MyPlugins/Porch Drive。Logic のプリセットと同じ場所）
- LINK の番号はプリセットで変わりません

## 入れ方
Mac の Logic Pro 用 AU です。GitHub の Releases の「latest」から `PorchDrive.zip` をダウンロードして解凍し、中の `.component` を
`~/Library/Audio/Plug-Ins/Components/` に入れます（くわしくは MyPlugins/README.md の「入れ方」）。
