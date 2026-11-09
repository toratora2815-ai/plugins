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

未登録で使っている番号: 17 = AIO Guitar、18 = Vox Chop、19 = Drum Transient、20 = Deca Satu（PluginLink.h には足していない。足すと Shared が変わって全部ビルドになるため）
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
