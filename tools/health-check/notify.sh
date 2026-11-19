#!/bin/bash
# 報告係（Claude アプリの定期実行）用: Mac に通知を出す（直したほうがいいものがあった朝だけ）。
# メッセージは引数のまま渡す（AppleScript に文字列として埋め込まないので、" などが入っても壊れない）
#   使い方: bash tools/health-check/notify.sh "<1 行の結論>"
/usr/bin/osascript - "${1:-自作プラグイン健康診断}" <<'APPLESCRIPT'
on run argv
  display notification (item 1 of argv) with title "自作プラグイン健康診断"
end run
APPLESCRIPT
