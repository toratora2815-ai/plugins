# MyPlugins

GitHubに置くだけで、Mac用・Windows用のプラグインが自動で作られるセットです。
（GitHub Actions という無料のクラウドビルド機能を使います）

## 手順
1. https://github.com でアカウントを作る（無料）
2. 右上の「+」→「New repository」→ 名前を入れて「Create repository」（Privateでも可）
3. 「uploading an existing file」を押し、この中身（WoolFuzz、Nebula、PorchDrive、BalanceScope、.github）をドラッグして「Commit changes」
   ※ Macでは .github フォルダが隠れて見えないことがあります（Finderで command + shift + . で表示）
     どうしても入らない場合は「Add file」→「Create new file」で、名前に .github/workflows/build.yml と入れ、
     中身に build.yml の内容を貼り付けて保存してください
4. 上の「Actions」タブを開くと自動でビルドが始まる（15〜25分ほど）
5. 終わったら実行結果を開き、下の「Artifacts」から欲しいものをダウンロード
   例：Nebula-macOS / Nebula-Windows

## インストール

### Mac
ダウンロードしたzipを解凍して、
- .component（AU：Logic用）→ ~/Library/Audio/Plug-Ins/Components/
- .vst3（Cubase / Studio One用）→ ~/Library/Audio/Plug-Ins/VST3/
に入れる。（Finderで option を押しながら「移動」メニュー→「ライブラリ」で開けます）

その後「ターミナル」で次の2行を実行（ネットから落としたファイルのブロックを解除）：
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components
    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3

Logicを起動してプラグインを再スキャン。

### Windows
解凍した .vst3 を
    C:\Program Files\Common Files\VST3\
に入れて、DAWで再スキャン。
