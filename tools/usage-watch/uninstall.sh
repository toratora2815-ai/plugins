#!/bin/bash
# 残量チェックを外す
PLIST="$HOME/Library/LaunchAgents/com.myplugins.usagewatch.plist"
launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null
rm -f "$PLIST"
/usr/bin/plutil -remove statusLine "$HOME/.claude/settings.json" 2>/dev/null
rm -rf "$HOME/.claude/usage-watch"
echo "外しました（~/Documents/Claudeの提案 の中身は残してあります）"
