# Forge Strip（フォージ・ストリップ）1.0.0

1 本のトラックに必要な処理をまとめたチャンネルストリップです。
この Mac に入っている定番のチャンネルストリップを、プラグインのまま直接動かして測り、
**音の振る舞い（効き方・時間の動き・音色）を数値で合わせたオリジナル**です（名前と見た目は別物）。

11 個のモジュールを好きな順番に並べて使えます。最後にリミッターがあります。

## 画面の使い方
1. 上の横長の列が「音が通る順番」です（左 → 右）。
2. 四角の **左の丸** をクリック = そのモジュールのオン / オフ。
3. 四角の **名前** をクリック = 下にそのモジュールのつまみが出ます。
4. 四角を **左右にドラッグ** = 順番の入れ替え。
5. いちばん右の **MASTER** = 入出力の音量・パン・幅・リミッター。
6. 各モジュールの右上の **MIX** = そのモジュールの効きを元の音と混ぜる（並列処理）。
7. 多くのモジュールは **BANDS** で 1〜3 帯域に分けて、帯域ごとに設定できます（SPLIT = 分け目の周波数）。

## モジュール
| 名前 | なにをする | 主なつまみ |
|---|---|---|
| SCULPTOR | 楽器の種類ごとの「理想の音色」へ、入ってきた音を自動で寄せる | TARGET（23 種類）/ AMOUNT / TONE（明るさ）/ SPEED / LOW・HIGH（効かせる範囲） |
| EQ | 12 帯域の EQ。13 種類の形（ベル、シェルフ 3 種×2、バンドシェルフ、ローカット/ハイカット） | 点をドラッグ、ホイールで Q。DYNAMIC にすると音量で量が変わる |
| COMP 1 / COMP 2 | コンプ（比を 1 より小さくすると小さい音を持ち上げる） | THRESH / RATIO / ATTACK / RELEASE / KNEE / GAIN / MIX、AUTO GAIN、VINTAGE、SC HPF/LPF |
| EXCITER | 倍音を足す。X と Y で 4 種類の歪み方を混ぜる | DRIVE / X / Y / BLEND、HARMONICS（倍音の色）、TAME、TONE |
| TRANSIENT | 鳴り始め（ATTACK）と余韻（SUSTAIN）を上げ下げ | ATTACK / SUSTAIN（±15 dB）/ CONTOUR、MODE |
| GATE | 小さい音を消す | THRESH / HYST（閉じる所を下げる）/ RATIO / ATTACK / RELEASE / HOLD |
| DENSITY | 小さい所を持ち上げて密度を出す（上向きのコンプ、先読みあり） | RANGE / RATIO / THRESH / SPEED |
| CLIPPER | 山を切る。SOFT BOOST で丸く | THRESH / SOFT BOOST |
| PHASE | 全帯域の位相を同じ角度だけ回す・遅らせる | ROTATION / DELAY |
| UNMASK | サイドチェーンの音（例: ボーカル）とぶつかる帯域だけ、こちらを下げる | AMOUNT / SENSITIVITY / ATTACK / RELEASE / LOW・HIGH |

## おすすめ設定
- **ボーカル**: SCULPTOR = Vocals・AMOUNT 50 → EQ で 100 Hz 以下を Flat High Pass → COMP 1（THRESH -24、RATIO 3、ATTACK 10、RELEASE 80）→ EXCITER（DRIVE 3、X 1、Y 0）
- **キック**: TRANSIENT（ATTACK +6、SUSTAIN -3）→ COMP 1（3 帯域、LOW だけ THRESH -20）→ CLIPPER（THRESH -3、SOFT BOOST 4）
- **オケ（ボーカルの裏）**: UNMASK をオン、Logic のプラグイン右上「サイドチェーン」でボーカルのトラックを選ぶ → AMOUNT 50
- **ベース**: SCULPTOR = Bass・AMOUNT 40 → DENSITY（RANGE 6、THRESH -30）→ COMP 2（VINTAGE、THRESH -20）

## 遅れ（レイテンシー）
Logic が自動で合わせます。オンにしたモジュールの分だけ増えます（48 kHz）:
SCULPTOR / UNMASK = 1024、DENSITY = 1662、TRANSIENT = 144、EXCITER / CLIPPER = 24（4 倍オーバーサンプリング）、リミッター = 48 サンプル。

## 合わせ込みの結果（基準との差、48 kHz）
- コンプ: 静的なカーブ 0.1 dB 以内、アタック/リリースは平均 0.1〜1 dB（アタック 100 ms・リリース 1 秒だけ 2 dB）
- EQ: ベル・シェルフ・バンドシェルフ 0.02〜0.5 dB（ローパスは 16 kHz より上だけ深め）
- ゲート 0.15 dB、Density 0.2〜0.8 dB、クリッパー 0.1 dB（倍音も一致）、トランジェント 0.1〜0.7 dB
- エキサイター: 倍音の比は多くが 1〜3 dB 以内（右上の角は粗め）
- SCULPTOR: 目標カーブ 0.5〜2 dB、UNMASK: 約 2 dB

## まだのもの（次の段階）
- コンプの PUNCH モード、Vintage の細かいカーブ、自動リリースの精密な合わせ込み
- EQ の Soft Saturation、ダイナミック EQ の時間の合わせ込み
- SCULPTOR の「Add Punch / Add Polish」、Phase を直線位相（基準は 50 ms 遅れ）に
- 別トラックどうしで見ながら混ぜる画面（Visual Mixer に当たるもの）

## 測定・テストの道具（tools/）
- `fs_cli.cpp` … wav を通して生データを書き出す（基準と同じ信号を通して比べる用）
- `test_engine.cpp` … 数値テスト（全部オフで元のまま、何もしない設定で遅れどおりに元の音、極端な設定で発振しない、など）
```
c++ -O2 -std=c++17 tools/test_engine.cpp -o /tmp/fs_test && /tmp/fs_test
```
