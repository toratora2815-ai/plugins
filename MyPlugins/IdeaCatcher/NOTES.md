# Idea Catcher — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Idea Catcher（IdeaCatcher）… 思いつきの鼻歌・口ドラムを録って、キーとテンポ入りの Logic プロジェクトにするメニューバー常駐アプリ（LSUIElement、Dock に出ない）。
録音キー（Carbon の RegisterEventHotKey、最初は ⌃⌥I、アクセシビリティ不要）で録音／停止、黙って 4 秒でも止まる。マイクは録るときだけ開く。
解析は IdeaAnalysis.h（JUCE なし）: Hum Keys の VoiceEngine.h / KeyFinder.h の写しを録音後にかける（1 回目で音程→キー、2 回目で音階に合わせる、ドラムは Hum Keys の設定 dproto/dhas/dsus を読む）、
鼻歌らしさ（音程のとれた割合）でメロディー／ドラム、テンポは出だしの自己相関。SMF（タイプ 1、テンポ・4/4・キー、Melody / Drums(ch10) トラック）を自前で書き、
NSWorkspace で Logic（com.apple.logic10）に開かせる＝新しいプロジェクトになる。保存は ~/Music/Idea Catcher/<日時>/（.mid・Voice.wav・info.json）。
分散通知 com.myplugins.ideacatcher.command（toggle / start / stop / show / open）。`--snapshot <フォルダ> [<wav>]` で画面を PNG に
（VoiceEngine.h / KeyFinder.h はもとは Hum Keys の写し。Hum Keys は 1.4.0 で消したので、今は Idea Catcher のもの）
v1.1.0: さかのぼり録音（設定 preroll、マイクを開いたまま ring に約 32 秒。押した所から前の声のかたまり＝2 秒黙った所まで、直前 5 秒に声がなければなし。"catch" で直前だけ）、
タップ（録音中だけ Space を registerExtraHotKey(1,…)、idea::tapGrid）、カウントイン（countIn / countBpm / clickDuring、クリックは出力へ。gridT0 = カウント後の 1 拍目＋入出力の遅れ、gridStep 4）、
重ね録り（Layer = Result＋kind、Voice 2.wav…、info.json の "layers"。前のレイヤーを effectiveGrid で固定して、1 小節目から小節ループで流しながら録る。テンポ・キー固定）、
HUD（Hud、mac::makeOverlay、このアプリが前面でないときだけ）、通知（UNUserNotificationCenter、autoOpen OFF のとき。クリックで Logic）、
声を Logic へ（Voice for Logic.wav を作り、mac::importAudioIntoLogic が AX で ⇧⌘I のメニュー項目を押して ⇧⌘G でパス入力。窓は MIDI のファイル名か 名称未設定 で見分ける。アクセシビリティ許可が要る、設定 voiceToLogic）、
MIDI に "Key Am" のマーカー。Num Deck 1.21.0 に Cat::IdeaCatcher（remote "ic:…" → sendRemote("ideacatcher", …)）
v1.2.0: マイクは Mac 本体に固定（設定 builtInMic、mac::builtInMicName が CoreAudio の Built-in で入力のある機器を探す。出力はそのまま）。
ノイズ除去（NoiseReduce.h、JUCE なし。30 Hz ハイパス→STFT 2048/512、静かな 15% の平均をノイズに、ならしたパワーでウィーナー風に下げる。設定 noise 0/1/2 = なし/ふつう -15 dB/強め -24 dB。
録り終えて解析する前にかけ、保存する声にも入る。静かな所と声の差が 12 dB 未満なら何もしない）
v1.3.0: Logic は自動で開かない（autoOpen 標準 OFF、設定 v13 で一度だけ OFF に。黄色いボタンは「MIDI を Finder で表示」、通知クリックは画面を出す）。
テンポを 2 つに: MidiOptions.bpm = 鼻歌のテンポ（秒→拍）、outBpm = 曲のテンポ（MIDI に書く。0 = 同じ、tempoOut()）。TEMPO の数字は juce::Label で打てる、info.json の "outBpm"
v1.3.1: アイコンから開くと画面を出す（mac::onReopen が NSApp の delegate に applicationShouldHandleReopen を class_addMethod で足す。JUCE は受けない）。
MIDI のコピーをダウンロードにも置く（設定 copyTo 1=ダウンロード / 2=デスクトップ / 0=置かない、info.json の "copy"。名前が変わったら前のコピーは消す。
動かした・消したコピーは読み直しで作り直さない）。通知クリックとメニュー「できた MIDI を Finder で表示」でそのコピーを Finder に
v1.4.0: Hum Keys をまとめた。口ドラムをなくした（DrumVoice・Hum Keys の設定を読む処理・kind 切替ボタンを削除。前の info.json の kind=1 のレイヤーは読まない）。
Logic を開かない（openLastInLogic・autoOpen・声の自動読み込みを削除。"open" は画面を出す）。ピアノロール（roll）をつかんで Logic へドラッグ。
idea::cleanMelody（解析の最後：同じ高さのかけら 40 ms 未満をつなぐ（すき間 0 はつながない）、しゃくり＝前後の 6 割未満・音階で 1 つ隣を吸収、
max(60 ms, 32 分) 未満をまとめる・ぽつんとしたら消す、オクターブ飛びで割れた音をつなぎ直す、小さくぽつんとした音を消す、重なりを切る）。
クオンタイズ MidiOptions.quantize 0〜4（なし/1/4/1/8/1/16/1/8 3連）＋ strength、出だしだけ寄せて長さそのまま、同じマスは長いほう、重なりは切る（place）。
info.json "quant2" / "strength"（前の "quantize" 1 = 1/16, 2 = 1/8 を読み替え）。初期値はテンポが分かっている録音（gridStep != 0）だけ props "quantKnown"（3）、ほかはなし。
calmVelocity（80〜110）。pickupBeats（1 拍目より前の音があれば 1 小節前から、"Bar 1" マーカー）。
EditPanel（手直し：選ぶ・↑↓・消す・J でつなぐ・⌘Z・最初の状態＝loadIdea (folder, true) で声から作り直す、4 小節表示＋ホイール）。手直ししたレイヤーは info.json の layers[i].notes に [秒, 秒, ノート, 強さ]。
Player.h（ToySynth＋声。MIDI・声・両方の聴き比べ、鼻歌のテンポで鳴らす）。命令 "edit"。--snapshot は idea_2.png に手直しの画面
