#!/bin/bash
# Claude Code のステータスライン。受け取った情報（5時間制限の使用率など）を保存して、画面の下に表示する。
DIR="$HOME/.claude/usage-watch"
mkdir -p "$DIR"
cat > "$DIR/statusline.json.tmp" && mv "$DIR/statusline.json.tmp" "$DIR/statusline.json"
F="$DIR/statusline.json"
get() { /usr/bin/plutil -extract "$1" raw -o - "$F" 2>/dev/null; }
used=$(get rate_limits.five_hour.used_percentage)
resets=$(get rate_limits.five_hour.resets_at)
model=$(get model.display_name)
if [ -n "$used" ]; then
  r=""
  if [[ "$resets" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    s=${resets%%.*}; [ "$s" -gt 100000000000 ] && s=$((s/1000))
    r=" / リセット $(date -r "$s" +%H:%M)"
  fi
  printf '%s  5時間: %s%% 使用%s' "$model" "${used%%.*}" "$r"
else
  printf '%s' "$model"
fi
