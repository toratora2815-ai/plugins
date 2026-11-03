#!/bin/bash
# Mac 側の「残量チェック」を入れる。リポジトリのフォルダで: bash tools/usage-watch/install.sh
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SRC/../.." && pwd)"
DIR="$HOME/.claude/usage-watch"
PLIST="$HOME/Library/LaunchAgents/com.myplugins.usagewatch.plist"

CLAUDE_BIN="$(command -v claude || true)"
[ -z "$CLAUDE_BIN" ] && [ -x "$HOME/.local/bin/claude" ] && CLAUDE_BIN="$HOME/.local/bin/claude"
if [ -z "$CLAUDE_BIN" ]; then
  echo "claude コマンドが見つかりません。先に Claude Code を入れてください。"; exit 1
fi

mkdir -p "$DIR" "$HOME/Library/LaunchAgents"
cp "$SRC/statusline.sh" "$SRC/check.sh" "$DIR/"
chmod +x "$DIR/statusline.sh" "$DIR/check.sh"
[ -f "$DIR/config.sh" ] || cat > "$DIR/config.sh" <<CONF
REPO="$REPO"
CLAUDE_BIN="$CLAUDE_BIN"
OUT_DIR="\$HOME/Documents/Claudeの提案"
MAX_USED=50      # 使用率がこれ以下なら
WINDOW_MIN=60    # リセットまでこの分数以内なら
CONF

# Claude Code のステータスラインを設定（使用率を受け取るため）
S="$HOME/.claude/settings.json"
LINE='{"type":"command","command":"~/.claude/usage-watch/statusline.sh"}'
if [ ! -s "$S" ]; then
  echo "{\"statusLine\": $LINE}" > "$S"
else
  cp "$S" "$S.backup-usage-watch"
  if /usr/bin/plutil -extract statusLine raw -o - "$S" >/dev/null 2>&1 || /usr/bin/plutil -extract statusLine json -o - "$S" >/dev/null 2>&1; then
    echo "（もともとステータスラインの設定があったので置き換えました。元の設定: $S.backup-usage-watch）"
  fi
  /usr/bin/plutil -replace statusLine -json "$LINE" "$S"
fi

# 10分ごとにチェックする
cat > "$PLIST" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.myplugins.usagewatch</string>
  <key>ProgramArguments</key><array><string>/bin/bash</string><string>$DIR/check.sh</string></array>
  <key>StartInterval</key><integer>600</integer>
  <key>EnvironmentVariables</key><dict>
    <key>PATH</key><string>$(dirname "$CLAUDE_BIN"):/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
  </dict>
</dict></plist>
P
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

echo ""
echo "入りました。"
echo "・Claude Code の画面の下に「5時間: ○% 使用」が出るようになります（次に開いたときから）"
echo "・使用 50% 以下で、リセットまで1時間を切ったら、提案を作って通知します"
echo "・提案の保存先: ~/Documents/Claudeの提案"
echo "・いまの値を見る: bash ~/.claude/usage-watch/check.sh --status"
