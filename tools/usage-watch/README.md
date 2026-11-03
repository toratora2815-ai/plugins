# usage-watch … 余った利用枠で Claude に提案を作らせる（Mac 側）

「5時間制限の使用率が 50% 以下」で「リセットまで1時間以内」になったら、
Claude が自分で「次に作るべきプラグイン・いまのプラグインの改善点」を3つ考えて、Mac に通知します。
気に入ったら Claude に「GO 2」のように返すだけ。

## 入れ方（1回だけ）
ターミナルで、このリポジトリのフォルダに移動して:
```
git pull
bash tools/usage-watch/install.sh
```
そのあと Claude Code を一度開いて何か話しかける（使用率の情報が届き始める）。
画面の下に「5時間: ○% 使用 / リセット ○○:○○」が出れば OK。

## 確認
```
bash ~/.claude/usage-watch/check.sh --status   # いまの使用率と残り時間
bash ~/.claude/usage-watch/check.sh --force    # 条件を無視して今すぐ提案を作ってみる
```
提案は `~/Documents/Claudeの提案/` に保存され、自動で開きます。

## 設定を変える
`~/.claude/usage-watch/config.sh` の `MAX_USED`（何%以下なら）と `WINDOW_MIN`（リセット何分前から）。

## 外す
```
bash tools/usage-watch/uninstall.sh
```

## 注意
- 使用率は「Mac の Claude Code が最後に受け取った値」。スマホや claude.ai で使った分は、Claude Code を次に使うまで反映されない
- Mac がスリープ中はチェックしない
- 提案の内容は `ideas/PROPOSAL_PROMPT.md` で決まる（クラウドの定期提案と共通）
