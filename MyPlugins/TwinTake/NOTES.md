# Twin Take — 作業メモ（Claude 用）

1 本録りの歌・ギターから「別テイク」を作るダブラー（aufx Twtk、Plugin Link Kind 25 = 未登録・LINK 欄だけ）。
2026-11-11 本人の GO（提案「Double Take」→ 実在製品名と同じなのでミックスエンジニア係の案で Twin Take に）。
試作の WAV（本人の歌「音出し」・ギター「Brit and Clean #11」、HS DOUBLE・MicroShift と聴き比べ）で本人「聞こえた」→ プラグインにした。

## 仕組み（Source/TwinTakeDSP.h、JUCE なし）
- 入力のモノ合計をリング（0.35 秒）に書き、ダブルは 16 タップの窓付き sinc（Kaiser β 8、カットオフ 0.95、512 相）で読む
- オンセット: 2 乗の速い包絡（0.3 / 10 ms）と遅い包絡（50 ms）の比が 9 dB を越えた所（4 dB を下回ったら再び待つ、70 ms あける、
  −70 dBFS より大きく、直近のピークの −30 dB より大きい）。位置は 1 ms 手前
- オンセットごとに、各声部が「遅れ・音程（読み出しの速さ）・音量・音色の傾き（800 Hz の 1 次の上下）」を選び直す。
  乱数は mix64（splitmix）に「曲の位置を 64 サンプルで丸めたもの・TAKE・トラック名のハッシュ・声部・つなぎ直しの回数」を入れる
- 切り替え: 古い読み出しはオンセットの 1.5 ms 手前で 0 になるよう 5 ms でフェードアウト、新しい読み出しはオンセットの 1.5 ms 手前から 1.5 ms でフェードイン。
  フェードは読み出し位置で決める（速さが 1 からずれても正確）。新しい読み出しは「オンセット − 1.5 ms ＋ 遅れ」の時刻に始まる（予定 = Spawn、声部ごとに 8 個、読み出し 6 個）
- 伸ばした長い音で遅れが範囲（VOCAL 11〜60 ms、GUITAR 10〜70 ms）を外れたら、音程は同じまま範囲の反対の端へ。
  古い読み出しがこれから読む 10 ms と、候補の所の相関がいちばん高いずれ（±5 ms）を探して 30 ms でつなぐ（forcedSplice・align）
- VOCAL: 遅れ = 15 + 10·L ± (2 + 12·L) ms（三角分布、12〜45 に制限。声部 3・4 は +4 ms）、音程 ±(2 + 7·L) × (0.25〜1) セント、音量 ±(0.5 + 2·L) dB、傾き ±(0.5 + 1.5·L) dB
  子音・息: 4 kHz の HP の 3 ms 平均 / 全体の 3 ms 平均 が 0.3〜0.6 で閉じる、直近ピークの −25 dB より小さくてざらざら（比 0.08 以上）でも閉じる。
  ゲートはリングに書き、読み出し位置と 2 ms 先の小さいほう（閉じるのは 0.5 ms、開くのは 20 ms）。ダブルの 7 kHz より上を約 −3 dB
- GUITAR: 原音を 40 ms 遅らせる（guitarLatencyMs、申告）。ダブルは 40 ± (10 + (6 + 9·L)·u) ms。前（−）か後ろ（＋）かは guitarSide()＝TAKE とトラックの種のハッシュで固定
  （最初は音ごとのくじだったが、後ろへ跳んだ所で右だけ最大 47 ms 途切れた。固定で 1 回の跳びは最大 6 + 9·L ms）。音程は「原音から離れる向き」（前にいるダブルは高め = さらに前へ）。
  原音はいつも LR4（2 次バターワース 2 段）で LOW KEEP で分け、高域を左（pan = −WIDTH）、低域は ON でまん中・OFF で高域と同じ左（OFF も LR4 を通す = ON⇄OFF でへこまない）
- LOW KEEP の OFF は、フィルターを外さず混ぜる量（lkMix）で消す（周波数を 0 へ動かすと 2 次の HP が不安定になるため）
- 入力のモノ合計が NaN・無限大・1e6 以上なら 0 にしてから解析・リングへ（1 サンプルで以後ずっと NaN になっていた）
- MODE の切り替え: 10 ms で下げてから切り替え、10 ms で戻す（switching / nextMode / swGain。latencySamples は予定のモード）
- VOICES: 声部ごとの左右の音量 panL / panR を 20 ms でなめらかに（パンと 4 本の 0.707 倍が一気に変わってプチッとしていた）
- 再生位置が飛んだら（timeline が前のブロックから「ブロック 1 つ＋10 ms」より大きくずれた。四捨五入・Varispeed の揺れは飛びとみなさない）jumped():
  VOCAL は読み出しを今の音量から 5 ms で消す（ループの切れ目では本物の 2 本録りも両方切りかわる）。始まる前に原音が切りかわる予定は捨てる。
  GUITAR は原音が 40 ms 遅れて出るので、リングの飛んだ所（w − kHalf）まで読み終えるまで鳴らす（すぐ消すとループの切れ目ごとに右だけ 15〜47 ms 抜けた）。
  どちらも検出をやり直し、飛んだ後の音の頭は飛んだ所より前を読まない（jumpW）
- オンセットで古い読み出しを消すとき（limit）、もう消える予定のものは触らず、今の音量より上げない（つなぎ直しのフェード中に 1.0 へ戻っていた）
- モノ出力（1→1）: ダブルは左右に分けず DOUBLE の大きさ（4 本の 0.707 倍も gMono でなめらかに）、原音はそのまま（パンで −3 / −6 dB にしない）
- 包み（PluginProcessor）: processBlock の頭で入力の NaN・無限大・1e6 以上を 0 に（GAIN MATCH の K 特性フィルターが NaN で壊れないように）
- パン: 定電力（cos / sin）。1 本 = まん中、2 本 = ±WIDTH、4 本 = ±WIDTH と ±WIDTH/2（4 本のときは 1 本ずつ −3 dB）
- MONO: 最後に (L+R)/2。画面のボタンを押している間だけ（パラメーターではない、保存しない）。GAIN MATCH は MONO の間 amount 0

## 1.1.0 で足したもの（2026-11-12、本人「全部直しましょう」→ ミックスエンジニア係が決めた形）
- 語尾（TAIL、frameDone / tailTarget）: 原音のモノ合計の 2 乗を 1 ms ごと（frameLen = round(0.001·fs)）にまとめ、10 ms の窓の平均（dB、−120 で下止め）を lvlDb（輪 1024）に。
  「範囲の終わり」E = VOCAL は今、GUITAR は 30 フレーム前（原音を出している w − 40 ms ＋ 10 ms。測るのが 10 ms 遅れる分を打ち消す）。
  E から前へ 100 フレーム、「そこから E までの一番小さい値」を sufMin に作り直す（1 ms ごと）。
  各 Head は 8 サンプルごとに、fc = 「pos ＋ 5 ms のサンプルの区切り」（= pos がまん中の 10 ms の窓、E まで）として ex = lvlDb[fc] − sufMin[fc] − 4 dB、
  ex > 0 なら 10^(−ex/20) を目標にし、下げる 3 ms・戻す 10 ms で追う（tailG）。VOCAL と、GUITAR でダブルが後ろ（guitarSide() > 0）のときだけ。
  効かない間（GUITAR で TAKE を後ろ → 前にした時など）は 10 ms で 1 へ戻す（固まらないように）。
  1 ms の区切りは曲の位置（tl）にそろえ（(tl+1) % frameLen == 0 で区切る。平均は frameAcc / frameCount）、サンプルごとの区切りの番号を frameOf（buf と同じ輪）に持つ。
  位置が飛んだら書きかけの区切りは捨てる。これで止まっていた長さが違っても同じ音（直す前は 10 ms ごとのダブルの大きさで最大 1.2 dB 違った）
  3 秒のピークとは比べない（大きいサビの後の小さいパートで何秒も消えるため）。「今」だけと比べない（短い切れ目の後に手前の母音がポコッと戻るため）。消したまま待たない（レガートで抜けるため）
- 長い音の中の揺れ（DRIFT、choose / driftStep）: VOCAL だけ。driftA = min(3, 0.5·|cents|) → 0 セントをまたがない。dFrom → dTo を smoothstep で segLen（1.5〜3 秒）かけて、
  rate は 32 サンプルごと。乱数は choose の Rnd から（曲の位置・TAKE・トラック・声部）。音の頭では 0 から。forcedSplice は Head を写すので続きから。サイン波にはしない（Echo Drift・HS と被る）
- DOUBLE ONLY: パラメーター doubleOnly（Bool、版のヒント 11、オートメーション不可）。dryS を 20 ms で 0 に（モノ出力・VOCAL・GUITAR の 3 か所）。
  myPresets.exclude・GainMatch の ignoreIds・内蔵プリセットの初期化から外す。GAIN MATCH は MONO・DOUBLE ONLY の間 amount 0。
  画面: 右の欄（VOICES・TAKE の下）のボタン、上の帯の注意書き（MONO TRACK より先に出す）、TAKES の ORIGINAL を薄く
- DSP 検査係（1.1.0、1 回目「直してから」）: ① sufMin を pos の区切りから引いていて、音の頭の直前の静かな所を拾い、ダブルの頭を下げていた
  （本人の歌で頭 20 ms が平均 −3.1 dB、ギターの右でいちばん下 −10.9 dB）→ fc で引く ② GUITAR で TAKE を後ろ→前にすると tailG が固まった → 1 へ戻す
  ③ 区切りが reset からのサンプル数で、再生のたびに最大 1.2 dB 違った → 曲の位置にそろえた。テストに「音の頭を下げない」「TAKE の切り替えで固まらない」「止まっていた長さが違っても同じ」を足し、
  「20 ms の切れ目」の窓を min(手前の遅れ − 6, 次の遅れ − 3) ms に直した。2 回目「渡してよい」（音の頭 0.00 dB、TAKE の切り替え −13.0 → −13.0 dB、再生ごとの差 −106〜−117 dBFS = 1.0.0 と同じ）。
  テスト 9 で残るサンプル単位の差（48 kHz 1e−5・192 kHz 1e−4）は 1.0.0 から: LOW KEEP の float の 2 次 HP（200 Hz）が丸めの 1 ビットの違いを低域でふくらませる。
  Biquad を double にすれば 3e−8 / 9e−8 まで消える（任意。gateHP・LR4 にも効く。まだやっていない）
- テスト（tools/render.cpp の test、44.1〜192 kHz）: パッと切れる語尾 25 ms 後 −53〜−56 dB / ゆっくり消える語尾（80 ms で −60 dB）で原音 −30 dB の所のダブル −27 dB（処理なし −7〜−14 dB）/
  20 ms の切れ目で戻らない −34.9〜−36.6 dB（窓は min(手前の遅れ − 6, 次の遅れ − 3) ms） / −12 dB のへこみで下げるのは 8.1〜8.2 dB / 揺れ ±3 セント以内・0 をまたがない・GUITAR は 0 / DOUBLE ONLY で原音が残らない・切り替えでプチッとしない。
  Params.tail はテスト用（処理なしと比べる）
- 次の候補（本人に聞いてから）: GUITAR はいつも同じ側が先に鳴る（TAKE で前後を固定しているため。先行音効果で「右がいつも走る / もたる」に聞こえやすい）。
  休みのあと（跳ぶ量 20〜50 ms 以上原音が無音だった音の頭）でだけ前後を選び直す案（ミックスエンジニア係、2026-11-12）

## プラグイン（Source/PluginProcessor.*）
- パラメーター ID: mode（Choice、オートメーション不可）、voices（Choice 1/2/4）、double（−40〜0 dB、−40 = OFF、skew 2）、loose、width（%）、
  lowKeep（60〜400 Hz、70 未満 = OFF）、take（Int 1〜8）、gainMatch（共通、版のヒント 10）、linkGroup
- MODE が変わったら apvts の Listener で setLatencySamples（GUITAR 40 ms、VOCAL 0）。setStateInformation の後も updateLatency
- 曲の位置: 再生中は getTimeInSamples、止まっているときは内部の数（freeRun）
- トラック名のハッシュ → trackSeed（同じ設定でもトラックごとに違うテイク）。一度だけ作って状態（ttSeed・ttTrack）に保存し、開いている間の改名では変えない。
  開き直したときに保存名と違えば（別のトラックへコピー）、または別のトラックの設定を読み込んだら、今のトラック名で作り直す
- 内蔵プリセット 6 個（Init / Lead Thick / Lead Double / Chorus Stack / Rap Tight / Guitar L-R）。全部を初期値に戻す処理から gainMatch・linkGroup は外す
- モノのトラック（1→1）に挿されたら画面の上に注意（monoTrack）。1→2 は対応

## テスト・道具
- tools/render.cpp … `render test`（44.1〜192 kHz × VOCAL 1/2/4・GUITAR: NaN・ピーク・ブロックの大きさで同じ・鳴り止む・GUITAR の遅れと左だけ・
  VOCAL の原音そのまま・LR4 が平ら・モノ出力・無音）。`render in.wav out.wav mode=… trimlat=1` で WAV を通す
- 聴き比べの道具（使い捨て、build/twintake）: abhost（AU に通す・cab・mono 切り出し）、norm.py（K 特性で大きさをそろえる）
- 健康診断（tools/health-check）: 1.0.0 の手元ビルドで異常・注意なし。参考は MODE の切り替えのプチッだけ（遅れが変わるので仕様）。CPU 0.5 %（48 kHz）

## DSP 検査係
- 1 回目「直してから」: NaN で戻らない・limit で音量が 1.0 へ戻る（+44 dB の段差、1 曲に 1 回ほど）・VOICES と MODE の切り替えのプチッ、
  ほか align の 44.1 kHz の重さ・GUITAR のつなぎ直し先が原音に近い・GUITAR の前後の入れかわり・LOW KEEP ON⇄OFF のへこみ・位置の飛び・モノ出力・種の保存 → 全部直した
- 2 回目「直してから」: 位置の飛びの直し方で GUITAR のループの切れ目に右が抜ける・位置の揺れで飛びと誤る・モノ出力の VOICES のプチッ・GAIN MATCH に NaN・
  ttTrack が空で保存される → 直した（GUITAR の抜け 0〜2.5 ms、揺れ・98% で飛び 0 回）
- 3 回目「渡してよい」: 新しい不具合なし。VOCAL の「飛んだら 5 ms で消す」も賛成（ループの頭が伸ばした音の途中だと 10〜30 ms 抜けるが、書き出しには出ない）。
  軽い提案（枠のコメント・keep は今のモードで・飛びの判定は max(n, 前のブロック) + 10 ms・README に VOCAL の抜け）を入れた
- 検査係のテスト一式（t_limit・t_knobs・t_gaps・t_cpu など）は scratchpad にあった（消える）。主な項目は tools/render.cpp の test() に入れた

## 版
- 1.0.0（2026-11-12）… 最初の版
- 1.1.0（2026-11-12）… 語尾（TAIL）・長い音の中の揺れ（DRIFT）・DOUBLE ONLY を追加
