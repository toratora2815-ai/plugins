# Mod Host — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Mod Host（ModHost）… 好きな AU エフェクトを中に読み込み（JUCE_PLUGINHOST_AU、AuList.cpp が AudioComponentFindNext で一覧、createPluginInstanceAsync）、
MOD 1〜4（LFO / ENV / SIDECHAIN / RANDOM / STEPS、ModEngine.h は JUCE なし、tools/test_mod.cpp）で中のどのつまみでも動かす。
1 MOD に 4 か所（ID m{n}a{t} が AMOUNT）、つなぎ先は LEARN（中のつまみを動かす＝audioProcessorParameterChanged）か一覧。
つないでいる間は 64 サンプルごとに中のプラグインを回し、元の位置（人が動かしたら更新）＋ MOD×AMOUNT を setValue。
保存は apvts＋STEPS＋PLUGIN（PluginDescription）＋PSTATE（base64）＋ROUTES（番号・名前・元の位置）。保存用の stateLock と音用の hostLock は別
v1.1.0: 通り道（Chain.h、JUCE なし、tools/test_chain.cpp）: 3 帯域 LR4（SPLIT LOW/HIGH、SEND LOW/MID/HIGH、ID bands）→ DRIVE → 中 → MIX（元の音はレイテンシー分 Delay）→ AUTO GAIN（ID autoGain）→ OUT。
Mod Host 自身のつまみも MOD の行き先（番号 internalBase=1<<20 ＋ k、保存は IROUTES）。MORPH（STORE A/B、違うつまみを morph で補間、切り替え式は 0.5 で、保存は MORPH の a/b）。
Analyzer.h: 入口（遅らせた元の音）と出口を M/S で FFT 4096、BANDS ページで帯域ごとの LEVEL 差・WIDTH（サイドの割合）の差とスペクトル
- 1.2.0（2026-11-09）: AUTO GAIN の表示名を GAIN MATCH にして、ボタンをプリセット欄の左端に移した（ID autoGain と DSP はそのまま）。下の欄を 2 段（footerH 72）に
