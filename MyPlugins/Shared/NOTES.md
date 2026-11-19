# Plugin Link — 作業メモ（Claude 用）

ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Plugin Link（MyPlugins/Shared/PluginLink.h・PluginLinkJuce.h）… 自作プラグイン同士の連携。
~/Library/Application Support/MyPlugins/PluginLink-v2.bin を mmap した共有メモリの掲示板（64 席・数値 24 個、atomic だけで読み書き、3 秒で不在、10 秒で席を回収）。
v2 で並びを変えたので v1（PluginLink-v1.bin）の古い版とはつながらない。席の並びを変えるときはファイル名と magic も変える。
各プラグインに ID `linkGroup`（OFF/1〜4）と、画面いちばん下の LinkBar（LINK の選択＋今の様子）。トラック名は updateTrackProperties で伝える。
FROM（相手選び、LinkBar::setSources）… apvts.state の "linkFrom" に「種類番号|トラック名」で保存、Endpoint::setPreferred で find がその相手を優先。
今つながっているもの: Balance Scope → Hush Field（出すぎの帯域にベルを足して強めに処理、FOLLOW）、Balance Scope → Fader Pilot（オケの音量）、
Fader Pilot → Horizon Spread（SPREAD を下げる）、Hush Field → Balance Scope（削り量の表示。並びは namespace tamer）、Gain Stage → Balance Scope（GAIN の表示）、
Fader Pilot → Lumen Rooms / Elder Verb / Meld Verb / Echo Drift（歌っている間リバーブ・エコーを VOCAL DUCK dB 下げる。plink::VocalDuck、ID linkDuck）、
Gain Stage → Wool Fuzz / Porch Drive（出口を TARGET にそろえる。plink::LevelMatch、ID linkMatch。RMS モードのときだけ）、
Fader Pilot / Balance Scope → Mod Host（MOD の種類 LINK）。
Sample Sculptor は LINK 欄だけ（自然な相手なし）。Kind 3 は前は Velvet Tamer、今は Hush Field
新しくつなぐときは Kind と送る数値の番号（namespace bs / fp / tamer / gs / ref / out のように）を PluginLink.h に足す

未登録で使っている番号: 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu、21 = My Tremolo、22 = My Filter、23 = My Crystal、24 = Noise Out、25 = Twin Take、26 = Retro Retro（Fader Pilot の歌でノイズを下げる）、27 = Hybrid Kit（LINK 欄だけ）（PluginLink.h には足していない。足すと Shared が変わって全部ビルドになるため）
空き番号（2026-11-06）: Kind 6 = Reference（プラグイン、削除）、Kind 7 = Nebula（削除）。番号は詰めない・使い回さない
（kindNebula の名前は残っているが、VocalDuck を受けるのは Lumen Rooms / Elder Verb / Meld Verb）。
Balance Scope の Reference を待つ処理は 1.3.1 で外した（namespace ref は PluginLink.h に残っているが、使っているものはない）。
namespace tamer は今は Hush Field の並び。名前を変えると Shared を使う全部のビルドになるので、次に Shared を触るときに直す。
Forge Strip / Forge Mixer は独自の種類 200 / 201（ForgeStrip/Source/ForgeLink.h）で、この一覧の外の独立した系統。Forge Mixer は 2026-11-06 に削除したが、200 / 201 は使い回さない。新しい Kind はこれとぶつけない。

## GainMatch.h（GAIN MATCH、2026-11-09）
全部のエフェクトに付ける「入れても・つまみをいじっても音量が変わらない」ボタン。ID `gainMatch`（既定 OFF、**版のヒント 10**。1 だと Logic の AU パラメーター番号がずれて既存曲のオートメーションが別のつまみに行く）。
- 測り方: 入口・出口を K 特性（BS.1770 の 2 段、どのサンプルレートでも同じ形）で、チャンネルの平均の 2 乗。100 ms 区切り。
  入口が −60 dB 以上の区切りと、止まってから 3 秒（tailSeconds）の区切りを、入口・出口の両方で数える（間のある歌＋エコーでも持ち上げすぎない）
- 測り直しのきっかけ: ON にした時 / parameterGestureChanged の終わり / Presets::onUserChange（画面で触った・プリセット・UNDO・REDO）/ requestRelearn()（内蔵プリセット）/
  開き直した直後にトラック名（setTrackName）が保存名 "gainMatchTrack" と違う時（= 別のトラックへコピー）。開いている間の改名では測り直さない。
  ジェスチャーなしの値の変化（オートメーションの再生）では測り直さない。JUCE の AU ではジェスチャーはプラグイン → ホストの一方向だけなので、
  Logic 側の操作（Smart Controls・コントロール表示・MIDI コントローラー）でも測り直さない。ignoreIds（BYPASS・DELTA など聴くだけのもの）は対象外
- 0.3 秒（鳴っている所）で仮の補正、10 秒（lockSeconds）で固定。補正は −24〜+12 dB、L/R 同じ、0.4 秒の指数でなめらかに
- amount（0〜1）: 0 で 0 dB に戻すだけで覚えた値は残す（MIX 100%、DELTA、SOLO、BYPASS）。0.05 以上跳ねたら 60 ms だけ 10 ms の速さで移す。リバーブ・ディレイは (100−MIX)/10
- limitBoost: 最後にリミッターがあるもの（Forge Strip の lim_on）は上げない。上げたいのに止めているときは readout の末尾に " LIM"
- 保存: gainMatchDb / gainMatchLocked / gainMatchTrack。loadFrom は atomic の受け取り待ち → process の頭で反映。固定済みなら開き直しても固定のまま
  （入口の音量で測り直すのはやめた: 歪み系は入口の大きさで補正が変わるので、静かなイントロから再生しただけで Wool Fuzz が −8.9 dB ずれた）。
  prepare 後の最初のブロックで受け取ったら、いきなりその値から（曲を開いた直後）
- 遅れ（process の latency 引数）の分、入口の測り値を 2^17 サンプルのリングで遅らせる
- ボタンは PresetBar::showGainMatch（プリセット欄の左端、幅 116）。その ID は Presets::exclude でプリセット・取り消しの対象外。PresetBar は狭いとき（中身 400 px 未満）< > を隠す
- 前からある Horizon Spread `autoGain`（±6 dB）、Fire Box `unity`（4 秒平均で追う）、Mod Host `autoGain`（約 0.5 秒で追う）は DSP そのまま、ボタンだけ showGainMatch に移した
- 出口のつまみ（OUTPUT / VOLUME など）も打ち消す（本人の「音量に何の影響も与えない」のとおり。触った後に測り直して数秒で戻る）。2026-11-09 本人に確認して「今のままでいい」
- 数値テスト: build/gmtest/main.cpp（コミットしない）。DSP 検査係の検査用ホストは scratchpad にあった（消える）
- 分かっている限界: Elder Verb の 49 秒のような長い響きを MIX 0.95 で使うと、3 秒の残り窓に入らない尾の分だけ +3 dB ほど大きくなる（MIX 0.85 なら +0.24 dB）
- 2026-11-19 NaN・無限大に強く（版は上げていない。Shared だけの直しなので）: 前は ON で測っている途中に入口へ NaN が 1 サンプル来ると K 特性のフィルターが NaN → curDb・targetDb が NaN → 出口がずっと NaN、
  Reset・OFF・prepareToPlay でも戻らず gainMatchDb="nan" が保存された（JUCE は "nan"・"inf"・"1e400" を NaN・無限大として読み戻す）。直す前の AU で測ると、GAIN MATCH と関係なく
  Elder Verb・Forge Strip・Lumen Rooms・Meld Verb・Porch Drive・Retro Retro・Wool Fuzz は NaN 1 回で出口がずっと NaN、My Filter・My Tremolo は +9 dB の大きな音のままだった。
  直し方: `usable(x) = |x| < 1e6`（NaN・±Inf・+120 dBFS 以上が false。Twin Take の入口と同じ線）。measureInput は非 const 参照になり、測ったあとで buf の全チャンネルの使えない値を 0 に書き換える
  （AIO Effecter のようにモノを左右に写してから measureInput (buf, 1) を呼ぶものもあるので全部。**入口を別のバッファへ写す・入口メーターを測るのは measureInput の後**）。
  測る側は手前の線 `measurable(x) = |x| < 100`（+40 dBFS。音は変えない）: それ以上・NaN は K 特性フィルターに 0、inPow・リングに負の印（badMark −1e30）→ その 100 ms の区切りは数えない（sinceActive だけ進める）。
  出口側も同じ（プラグイン自身の NaN は出口では直さない＝健康診断で見える）。+40 dBFS の線は DSP 検査係の指摘: +40〜+119 dBFS の有限の値 1 サンプルで、頭打ちするプラグイン（ファズ）の補正が最大 +17.8 dB 狂ったまま固定・保存されていた（前からの穴）。
  measureInput は先に全チャンネルを途中で抜けずに調べ（ベクトル化される）、普通の音なら直す前と同じ分岐なしの式で測る（CPU は直す前とほぼ同じ。分岐をループに入れた最初の案は +45〜85 %）。
  前のプラグインが 100 ms に 1 回以上 NaN を出し続けると、区切りが全部飛ぶので測れないまま（補正 0 dB・表示「GAIN MATCH ...」のまま。安全側なので仕様）。
  targetDb・curDb は有限でなければ 0、limitBoost (NaN) は 0、saveTo は有限でなければ 0・固定なし、loadFrom は非有限なら 0 dB・固定なし（ON なら次に鳴らした所から測り直す）。
  普通の音は 1 ビットも変わらない（今までの gmtest 全部の出口のハッシュが新旧で同じ）。テスト: build/gmtest/nan_tests.inc（gm_select.h で old/GainMatch.h と切り替え）、AU は build/gmnan/aunan.cpp（コミットしない）。
  Forge Strip は measureInput を processBlock の頭へ移し、サイドチェーンも同じ線で直した（1.3.2）。GainMatch.h を使わない Horizon Spread（1.4.2）・Fire Box（1.2.2）も NaN 1 回で出口がずっと NaN だったので、
  processBlock の頭で同じ線の 0 直し。Hush Field のサイドチェーンは偶然戻っていただけ（SC LISTEN では NaN が出た）なので HushEngine.h で 0 に（1.2.3）。
  PluginLinkJuce.h の LevelMatch: Gain Stage から届く targetDb が NaN だと jlimit を素通りして gainDb が NaN のまま戻らない → std::isfinite で弾く。
  Mod Host・Gain Stage・Fader Pilot は NaN が来ても戻る（DSP 検査係が入っている AU で確認）
  残した軽いもの（前から・急ぎではない）: Horizon Spread の autoGain は +40 dBFS 以上の 1 サンプルで最大 −2.9 dB が数秒（±6 dB で止まる。直すなら SpreadEngine.h:410 の eIn・eOut に同じ線）。
  Plugin Link の受け取り側（HorizonSpread/Source/PluginProcessor.cpp:269 の rideDb、ModHost/Source/PluginProcessor.cpp:301・303）も NaN なら jlimit を素通りする形だが、送る側（Fader Pilot・Balance Scope）が NaN を出さない作りなので今は起きない
