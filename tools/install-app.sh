#!/bin/bash
# 単体アプリを Releases の latest から入れて、この Mac 専用の署名で署名し直す。
# 署名が毎回同じなので、アクセシビリティなどの許可が更新のたびに外れない。
#   使い方: tools/install-app.sh <名前>      例: tools/install-app.sh NumDeck
# 署名がない Mac では、いつもどおりアドホック署名のまま入れる。
set -euo pipefail

NAME="${1:?アプリの名前（例: NumDeck）を指定してください}"
GH="${GH:-$HOME/.local/bin/gh}"
REPO="toratora2815-ai/plugins"
KC="$HOME/Library/Keychains/myplugins-signing.keychain-db"
PASSFILE="$HOME/.config/myplugins/signing-keychain-pass"
IDENTITY="MyPlugins Local Signing"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

"$GH" release download latest -R "$REPO" -p "$NAME.zip" -D "$WORK"
ditto -x -k "$WORK/$NAME.zip" "$WORK/app"
APP="$(find "$WORK/app" -maxdepth 1 -name '*.app' | head -1)"
APPNAME="$(basename "$APP")"

if [ -f "$KC" ] && [ -f "$PASSFILE" ]; then
    security unlock-keychain -p "$(cat "$PASSFILE")" "$KC"
    codesign --force --deep --keychain "$KC" --sign "$IDENTITY" "$APP"
    echo "この Mac 専用の署名で署名しました"
fi

osascript -e "quit app \"${APPNAME%.app}\"" 2>/dev/null || true
sleep 1
mkdir -p "$HOME/Applications"
rm -rf "$HOME/Applications/$APPNAME"
cp -R "$APP" "$HOME/Applications/"
xattr -dr com.apple.quarantine "$HOME/Applications/$APPNAME" 2>/dev/null || true
codesign -dr - "$HOME/Applications/$APPNAME" 2>&1 | tail -1
open "$HOME/Applications/$APPNAME"
echo "入れました: ~/Applications/$APPNAME"
