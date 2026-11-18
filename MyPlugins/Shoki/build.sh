#!/bin/bash
# 書記（Shoki）を Xcode なしで組み立てる（Command Line Tools の swiftc だけ）
#   bash MyPlugins/Shoki/build.sh          … build/Shoki/Shoki.app を作る
#   bash MyPlugins/Shoki/build.sh install  … 作って、この Mac 専用の署名をして ~/Applications に入れる
#   bash MyPlugins/Shoki/build.sh test     … 数値テスト（build/Shoki/shoki-test）を作る
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
OUT="$ROOT/build/Shoki"
APP="$OUT/Shoki.app"
mkdir -p "$OUT"
CORE=(Source/Capture.swift Source/Transcriber.swift Source/Voice.swift Source/Meeting.swift)
FLAGS=(-O -parse-as-library -target arm64-apple-macos26.0 -module-cache-path "$OUT/modcache")

case "${1:-app}" in
  test)
    swiftc "${FLAGS[@]}" -o "$OUT/shoki-test" "${CORE[@]}" tools/test_main.swift
    echo "OK: $OUT/shoki-test"
    ;;
  app|install)
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
    swiftc "${FLAGS[@]}" -o "$APP/Contents/MacOS/Shoki" "${CORE[@]}" Source/App.swift
    cp Info.plist "$APP/Contents/Info.plist"
    [ -f Resources/AppIcon.icns ] && cp Resources/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
    KC="$HOME/Library/Keychains/myplugins-signing.keychain-db"
    PASSFILE="$HOME/.config/myplugins/signing-keychain-pass"
    IDENTITY="MyPlugins Local Signing"
    if [ -f "$KC" ] && [ -f "$PASSFILE" ]; then
      security unlock-keychain -p "$(cat "$PASSFILE")" "$KC"
      codesign --force --deep --keychain "$KC" --sign "$IDENTITY" "$APP"
      echo "署名: $IDENTITY"
    else
      codesign --force --deep --sign - "$APP"
      echo "署名: アドホック（この Mac 専用の署名が見つからないため）"
    fi
    if [ "${1:-app}" = install ]; then
      mkdir -p "$HOME/Applications"
      rm -rf "$HOME/Applications/Shoki.app"
      cp -R "$APP" "$HOME/Applications/Shoki.app"
      echo "OK: ~/Applications/Shoki.app"
    else
      echo "OK: $APP"
    fi
    ;;
  *)
    echo "使い方: build.sh [app|install|test]"; exit 1 ;;
esac
