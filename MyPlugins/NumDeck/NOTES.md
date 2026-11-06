# Num Deck — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Num Deck（NumDeck）… テンキー 1〜9 に Logic の操作を割り当てる単体 Mac アプリ（ゲーム機メニュー風の 3×3 タイル画面）。
CGEventTap でテンキーを横取り → Logic の初期キーコマンドを送る（アクセシビリティ許可が必要）。3 ページ（/ と * で切替、v1.7.0 までは −/+）、
Clear（NumLock）で ON/OFF、「Logic が前面のときだけ」（標準 ON）、タイルクリックで割り当て変更・ショートカットを覚えさせる
（※ 今の本人の設定は Home = ON/OFF、Clear = モード切替〔v1.14.0〕。「.」の動きは v1.18.1 が最新）
v1.1.0: モード（Logic Pro / 業務、クリックでだけ切替）、各 4 ページ。業務はどのアプリでも動く（コピー等・タブ操作・アプリを開く・ページを開く）。
Chrome の前面タブを AppleScript で登録。仕事のリンク（URL）は公開リポジトリに書かず、本人の設定ファイル（~/Library/Application Support/Num Deck）にだけ入れる
v1.2.0: 小さい画面（幅 640 / 高さ 440 未満でコンパクト配置、設定から 430×300 に切替）、ウィンドウの位置と大きさを保存
v1.3.0: Logic Pro モード 4 ページ目「リファレンス」で Reference Monitor を操作（分散通知 com.myplugins.referencemonitor.command、
object に "ab" / "mix" / "ref" / "play" / "cue1" など。Reference Monitor 1.3.0 の RemoteCommands が受け取る）
v1.3.1: 1 ページ目の 3（キャプチャ録音）を Reference Monitor の出力 A/B 切替（"out" / "outA" / "outB"）に
v1.3.2: 1 ページ目の 1（先頭へ）をトラックのフリーズに（⌃⌥⇧⌘F を送る。Logic のキーコマンド「トラックのフリーズを切り替え」に本人が 1 回覚えさせる）
v1.3.3: Logic Pro モードのキー送信は前面アプリに関係なく Logic へ（KeyTap::sendKeyToLogic。Logic は後ろにいる間のキーを受けないので一瞬前に出して送り、150 ms 後に元のアプリへ戻す）。アプリのアイコン（Resources/Icon.png、tools/make-app-icons.swift で描く。Reference Monitor 1.3.2 も同じ）
v1.4.0: テンキー 0 の欄（横長タイル、添字 9、設定キー slot{p}_0）。1 ページ目の 0 は入力モニタリング（⌃⌥⇧⌘I、Logic で 1 回覚えさせる）
v1.5.0: アプリのタイルは前面ならもう一度押すと隠して開く前のアプリへ戻る。リンクを開く Chrome のアカウントを設定で選べる（Local State を読む、--profile-directory で開く）
v1.6.0: テンキー . の欄（添字 10、設定キー slot{p}_dot）。1 ページ目の . は低レイテンシモード（⌃⌥⇧⌘L、Logic で 1 回覚えさせる）
v1.7.0: モード切替キー（最初は Home=115、設定で覚えさせる。特殊キーは NX_SYSDEFINED を 1000+番号で扱う）。全画面アプリも、戻り先アプリを NSWorkspace で開き直して戻る
v1.8.0: ページ切替を / と * に（− と + は通す）
v1.9.0: リンクを 2 画面で開く（Slot の split: "chrome" か アプリの場所。Chrome --new-window で開き、アクセシビリティで左右半分に並べる）
v1.10.0: 2 画面は「ウィンドウ→フルスクリーンのタイル表示→画面左側」を AX で押し、相手選びの画面で右半分の真ん中（幅 0.875, 高さ 0.5）をクリックして全画面分割に。アプリのページの 0 = アプリを全部終了（"!quitAll"）
v1.11.0: タイルをドラッグで入れ替え（Deck::swapSlots）。2 画面: 全画面のウィンドウは AXFullScreen を外してから並べる、起動待ちは最大 20 秒
v1.11.1: アプリのタイル: そのアプリが全画面の左右分割中（AXFullScreen なのに画面幅の 9 割未満）なら、分割を解いてそのアプリだけ全画面に
v1.12.0: リファレンスのページの 0 = TONE / WIDTH 切替（"width"）、SNAP（"snap"）も選べる
v1.12.1: 前面のアプリのタイルをもう一度押したとき、全画面（1 枚）なら AXFullScreen を外して 1.3 秒後に隠す
v1.13.0: ページのタブをドラッグで並べ替え、設定の「出すページ」で隠す（設定キー pageOrder / work_pageOrder、例 "0,1,3"。隠したページの割り当ては残る）
v1.14.0: ON/OFF のキーも覚えさせられる（toggleKey = ToggleLearned(3) + toggleKeyCode）。本人の設定は Home=ON/OFF、Clear(71)=モード切替
v1.15.0: アプリのページの . = 全画面 ⇔ 半画面（"!fullHalf"、KeyTap::toggleFullScreenOrSplit。分割の相手は AX の窓一覧から探して覚え、戻すときは makeFullScreenSplit）
v1.16.0: マクロ（Slot type "macro"、steps = key / action / wait / app / url。Deck::MacroRunner がタイマーで 1 行ずつ、キーの間 80 ms。
Logic Pro モードは KeyTap::bringLogicFront で 1 回だけ前に出し、最後に returnFromLogic。同じキーをもう一度で中止。画面は MacroEditor.cpp の別ウィンドウ）。
リンクごとの Chrome アカウントと開く場所（LinkPlace: profile / screen（NSScreen.localizedName）/ region = full・left・right・top・bottom。
KeyTap::openURLPlaced が --new-window で開いて AX で置き、前が全画面ならそちらに戻す）
v1.17.0: 押した操作の HUD（KeyTap::showHUD、設定 showHud）。2 画面は左右の 2 アプリ以外を一時的に隠してから相手選び、入らなければ HUD で案内。リンクごとに左右のウィンドウを覚えて使い回す（linkWindows）。全部終了で残すアプリ（keepApps、前方一致、最初は Claude と com.apple.logic）。全画面⇔半画面の相手は 6 組まで。設定の書き出し・読み込み
v1.17.1: 全画面まわりの決め打ちの待ち時間をやめ、waitFor で状態（AXFullScreen・前面のウィンドウ）を確かめてから次へ。2 分割の前に左のウィンドウへキーボードを向け（focusWindow / isFocused）、別のウィンドウを分割しないように。リンクの使い回しは 2 つがお互いと分割されているときだけ「移るだけ」。アプリを前に出すのは bringAppForward（activateWithOptions は断られることがある）
v1.18.0: 2 画面のリンクは、そのページ（host）のタブがある Chrome のウィンドウを AppleScript で探して使い回す（入れ直し後も見つかる。分割済みならそこへ移るだけ）。全画面（相手を覚えていない）で「.」→ その前に使っていたアプリと半画面（NSWorkspace の切り替え通知で順番を記録）。「いつも手前」のとき Num Deck の全ウィンドウ（メニュー・マクロ画面も）に CanJoinAllSpaces+FullScreenAuxiliary を付け、ほかのアプリの全画面の上にも出す
v1.18.1: 「.」（全画面 ⇔ 半画面）は、全画面のとき全画面を解除するだけ（2 画面には戻さない。相手の記憶と前のアプリの記録は削除）
v1.18.2: 全画面の上に出すには、「いつも手前」のあいだ NSApp を Accessory（補助アプリ）にする必要があった（Regular のままだと CanJoinAllSpaces+FullScreenAuxiliary でも出ない）
v1.19.0: ウィンドウを動かす部分を Windows_mac.mm に分けた（入口は Windows.h の numdeck::win）。メニューバーのアイコン（KeyTap::setStatusMenu: 画面を出す / テンキー ON / いつも手前 / 終了）
v1.19.1〜1.19.2: 全画面の出し入れは setFullScreenSurely（切り替わったか確かめて頼み直す。アニメーション中だと無視されるため）。
右がアプリの 2 画面は、先にそのアプリを前に出してそのデスクトップへ移り、左のページがそこに無ければそこに新しく開く（相手選びには今のデスクトップのウィンドウしか出ない）。
使い回す左が単独の全画面なら先に解除。Chrome への AppleScript は with timeout of 3 seconds。動きの記録は ~/Library/Logs/Num Deck.log
v1.19.3: 2 画面の相手選びは、外れたら場所を変えて最大 5 回やり直す（何もない所で閉じた → 並べ直し、違う相手が入った → 2 つとも全画面を解いて並べ直し）。
開く場所付きのリンク（マクロの中も）も、そのページのタブがある Chrome のウィンドウを使い回す（Google の書類は /d/番号 で見分ける）
v1.20.0: 割り当ての分類に「Hum Keys」（Cat::HumKeys、remote が "hk:" で始まるものは sendRemote("humkeys", …)）。ページは足さず、本人が好きなキーに置く。
Hum Keys が前面のときも「Logic が前面のときだけ」の対象（isRefMonitorFrontmost が humkeys も見る）
v1.21.0: 分類「Idea Catcher」（Cat::IdeaCatcher、remote "ic:" → sendRemote("ideacatcher", …)。toggle / overdub / catch / tap / open / show）。Idea Catcher が前面のときも「Logic が前面のときだけ」の対象

v1.22.0: タイルの種類に「Aside（ひとりごと秘書）」（"as:memo" / "as:undo" / "as:show" → 分散通知 com.myplugins.aside.command）
v1.23.0: 「トラック」にオートメーションの Latch ⇔ Read（⌃⌥⇧⌘A）/ Latch に（⌃⌥⇧⌘T）/ Read に（⌃⌥⇧⌘R）。最初の割り当てには入れない、Logic に 1 回覚えさせる
v1.24.0: Hum Keys をやめたので分類「Hum Keys」（Cat::HumKeys・"hk:"）を消した。icOpen（"ic:open"）は「できた MIDI を出す（ドラッグ用）」（Idea Catcher 1.4.0 から Logic は開かず、画面を出すだけ）
v1.24.1: アプリのタイルで前面のアプリを隠すとき、戻る先が分からなくても Finder を開き直さない（Finder はウィンドウがないと新しいフォルダを出すため）。戻る先は「このタイルで開く前のアプリ」→「直前に使っていたアプリ（lastFrontPid）」→ なし。Finder は戻る先にしない（前にも出さない）。Num Deck 自身のタイルは、ほかのアプリに触らず Num Deck の画面を隠す・出すだけ（[NSApp hide:] / unhide + orderFrontRegardless）
