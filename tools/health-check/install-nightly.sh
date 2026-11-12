#!/bin/bash
# 毎晩 3 時の健康診断（測るところまで）を、Mac の自動実行（launchd）に登録する。Claude の利用枠は使わない。
#   登録: bash tools/health-check/install-nightly.sh
#   外す: bash tools/health-check/install-nightly.sh --remove
# 登録先: ~/Library/LaunchAgents/com.myplugins.health-check.plist（ログインしている間だけ動く。
#   3 時に Mac が寝ていたら、次に起きたときに 1 回動く）。記録は build/health/nightly.log
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
LABEL="com.myplugins.health-check"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

/bin/launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
if [ "${1:-}" = "--remove" ]; then
  rm -f "$PLIST"
  echo "毎晩の健康診断を外しました"
  exit 0
fi

mkdir -p "$HOME/Library/LaunchAgents" "$REPO/build/health"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/bash</string>
    <string>$REPO/tools/health-check/nightly.sh</string>
    <string>--nightly</string>
  </array>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Hour</key>
    <integer>3</integer>
    <key>Minute</key>
    <integer>0</integer>
  </dict>
  <key>WorkingDirectory</key>
  <string>$REPO</string>
  <key>StandardOutPath</key>
  <string>$REPO/build/health/nightly.log</string>
  <key>StandardErrorPath</key>
  <string>$REPO/build/health/nightly.log</string>
  <!-- CPU の数値を手で測ったときとそろえるため、アプリと同じ扱いにする（指定しないと CPU を絞られる） -->
  <key>ProcessType</key>
  <string>Interactive</string>
</dict>
</plist>
EOF
/usr/bin/plutil -lint "$PLIST" >/dev/null
/bin/launchctl bootstrap "$DOMAIN" "$PLIST"
echo "毎晩 3:00 の健康診断を登録しました（$PLIST）"
/bin/launchctl print "$DOMAIN/$LABEL" | grep -E '^\s*(state|path) =' | head -2
