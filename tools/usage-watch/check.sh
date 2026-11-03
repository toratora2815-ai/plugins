#!/bin/bash
# 10分ごとに launchd から呼ばれる。
# 「5時間制限の使用率が 50% 以下」かつ「リセットまで1時間以内」なら、Claude に提案を作らせて通知する。
# 使い方: check.sh          … 普段の自動チェック
#         check.sh --status … いまの値を表示するだけ（動作確認用）
#         check.sh --force  … 条件を無視して今すぐ提案を作る（動作確認用）
DIR="$HOME/.claude/usage-watch"
source "$DIR/config.sh" || exit 1      # REPO, CLAUDE_BIN, OUT_DIR, MAX_USED, WINDOW_MIN
F="$DIR/statusline.json"
LOG="$DIR/check.log"
get() { /usr/bin/plutil -extract "$1" raw -o - "$F" 2>/dev/null; }
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

to_epoch() {  # 数字（秒 or ミリ秒）でも ISO 形式の文字でも秒に直す
  local v="$1"
  if [[ "$v" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    v=${v%%.*}; [ "$v" -gt 100000000000 ] && v=$((v/1000)); echo "$v"
  elif [ -n "$v" ]; then
    v=${v%Z}; v=${v%%.*}; v=${v%%+*}
    TZ=UTC date -j -f "%Y-%m-%dT%H:%M:%S" "$v" +%s 2>/dev/null
  fi
}

used=$(get rate_limits.five_hour.used_percentage); used=${used%%.*}
resets=$(to_epoch "$(get rate_limits.five_hour.resets_at)")
now=$(date +%s)

if [ "$1" = "--status" ]; then
  if [ -z "$used" ] || [ -z "$resets" ]; then
    echo "まだ使用率の情報がありません。Claude Code を開いて1回なにか話しかけてから、もう一度試してください。"
  else
    echo "5時間制限: ${used}% 使用 / リセットまで あと $(( (resets-now)/60 )) 分（$(date -r "$resets" '+%H:%M')）"
    echo "条件: 使用 ${MAX_USED}% 以下 かつ リセットまで ${WINDOW_MIN} 分以内 → 提案を作る"
  fi
  exit 0
fi

if [ "$1" != "--force" ]; then
  [ -n "$used" ] && [ -n "$resets" ] || exit 0
  left=$((resets - now))
  [ "$left" -gt 0 ] && [ "$left" -le $((WINDOW_MIN*60)) ] || exit 0
  [ "$used" -le "$MAX_USED" ] || exit 0
  [ "$(cat "$DIR/last_window" 2>/dev/null)" = "$resets" ] && exit 0   # この5時間枠ではもう作った
  echo "$resets" > "$DIR/last_window"
fi

mkdir "$DIR/lock" 2>/dev/null || exit 0
trap 'rmdir "$DIR/lock"' EXIT
log "提案を作ります（使用 ${used}%）"

mkdir -p "$OUT_DIR"
out="$OUT_DIR/$(date +%Y-%m-%d-%H%M).md"
cd "$REPO" || { log "リポジトリが見つかりません: $REPO"; exit 1; }
/usr/bin/git pull -q --ff-only >> "$LOG" 2>&1
"$CLAUDE_BIN" -p "ideas/PROPOSAL_PROMPT.md を読んで、その指示どおりに提案を3つ作ってください。いまは「Mac の残量チェックから動いているとき」です。" \
  --allowedTools "Read" "Grep" "Glob" "Bash(git log:*)" "Bash(ls:*)" \
  > "$out" 2>> "$LOG"

if [ -s "$out" ]; then
  log "できました: $out"
  /usr/bin/osascript -e "display notification \"使い切れない枠で提案を3つ作りました。読んで、Claude に『GO 2』のように返信してね\" with title \"Claude からの提案\" sound name \"Glass\""
  /usr/bin/open "$out"
else
  log "提案の作成に失敗しました"
  rm -f "$out"
fi
