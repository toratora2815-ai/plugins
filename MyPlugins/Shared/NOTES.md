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
Fader Pilot → Lumen Rooms / Elder Verb / Meld Verb（歌っている間リバーブを VOCAL DUCK dB 下げる。plink::VocalDuck、ID linkDuck）、
Gain Stage → Wool Fuzz / Porch Drive（出口を TARGET にそろえる。plink::LevelMatch、ID linkMatch。RMS モードのときだけ）、
Fader Pilot / Balance Scope → Mod Host（MOD の種類 LINK）。
Sample Sculptor は LINK 欄だけ（自然な相手なし）。Kind 3 は前は Velvet Tamer、今は Hush Field
新しくつなぐときは Kind と送る数値の番号（namespace bs / fp / tamer / gs / ref / out のように）を PluginLink.h に足す

空き番号（2026-11-06）: Kind 6 = Reference（プラグイン、削除）、Kind 7 = Nebula（削除）。番号は詰めない・使い回さない
（kindNebula の名前は残っているが、VocalDuck を受けるのは Lumen Rooms / Elder Verb / Meld Verb）。
Balance Scope には Reference を待つコード（ref::listeningRef、FROM の候補）がまだ残っている。害はないので、次に Balance Scope を直すときに外す。
namespace tamer は今は Hush Field の並び。名前を変えると Shared を使う全部のビルドになるので、次に Shared を触るときに直す。
Forge Strip / Forge Mixer は独自の種類 200 / 201（ForgeStrip/Source/ForgeLink.h）で、この一覧の外の独立した系統。新しい Kind はこれとぶつけない。
