#!/bin/bash
# 自作プラグインの健康診断（数値の検査まで）。
#   毎晩: Mac の自動実行（launchd。tools/health-check/install-nightly.sh で登録）が 3 時に `nightly.sh --nightly` で動かす。
#         Claude の利用枠は使わない。結果を読んで「不具合か仕様か」を報告するのは、朝の Claude の定期実行
#   手で動かすとき（~/plugins で。Logic が開いていても待たずにすぐ始まる）:
#     bash tools/health-check/nightly.sh                    入っている自作 AU を全部
#     bash tools/health-check/nightly.sh --only Ecdr,Mytr   一部だけ（コードかフォルダ名）
#     bash tools/health-check/nightly.sh --groups basic,click --skip-auval   検査を選ぶ
# 結果: build/health/runs/<日時>/summary.md（一覧）・summary.json・raw/（細かい数値と記録）。
#   最新は build/health/latest、毎晩の最新は build/health/nightly、毎晩の様子は build/health/nightly-status.txt
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO/tools/health-check/hc.cpp"
OUT="$REPO/build/health"
BIN="$OUT/hc"
STATUS="$OUT/nightly-status.txt"
mkdir -p "$OUT/runs" "$OUT/reports"

NIGHTLY=0; FULL=1; ARGS=()
for a in "$@"; do
  case "$a" in
    --nightly) NIGHTLY=1 ;;
    --only|--groups) FULL=0; ARGS+=("$a") ;;   # 一部だけの回は「前回」として比べる相手にしない
    *) ARGS+=("$a") ;;
  esac
done

say() { echo "$(date '+%m-%d %H:%M') $*"; }
# 毎晩の回の様子を 1 つのファイルに書く（朝の Claude が読む）
write_status() {
  [ "$NIGHTLY" -eq 1 ] || return 0
  { echo "DATE=$(date +%Y-%m-%d)"; echo "TIME=$(date +%H:%M)"; for kv in "$@"; do echo "$kv"; done; } > "$STATUS.tmp" \
    && mv -f "$STATUS.tmp" "$STATUS"
}
write_status "STATUS=running" "SINCE=$(date +%H:%M)"

# 検査ツールを作る（ソースのほうが新しいときだけ）
if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  /usr/bin/clang++ -std=c++17 -O2 -Wno-deprecated-declarations -o "$BIN" "$SRC" \
    -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreFoundation \
    || { say "検査ツール（hc）のビルドに失敗しました"; write_status "STATUS=failed" "REASON=検査ツール（hc）のビルドに失敗しました"; exit 1; }
fi

# 毎晩の回（--nightly）だけ: Logic が再生・録音していて CPU を使っている間は待つ（作業中の音と CPU を取り合わないように）。
# 開いているだけで止まっているなら待たない（開いたまま寝ても、毎晩ちゃんと動くように）。手で動かすときは待たない。
# 止まっている Logic は 1 コアの 5〜10 %、鳴らしているともっと上がるので、20 % を目安にする
logic_pid() { { /usr/bin/pgrep -x "Logic Pro"; /usr/bin/pgrep -x "Logic Pro X"; } 2>/dev/null | head -1; }
logic_cpu() {   # Logic の CPU（1 コアの %。ps の値はもともと約 1 分の平均）を 30 秒で 4 回測った平均。開いていなければ何も出さない
  local pid s=0 i c
  pid="$(logic_pid)"; [ -z "$pid" ] && return 0
  for i in 1 2 3 4; do
    c="$(/bin/ps -o %cpu= -p "$pid" 2>/dev/null | tr -d ' ')"; [ -z "$c" ] && return 0
    s="$(/usr/bin/awk -v a="$s" -v b="$c" 'BEGIN { print a + b }')"
    [ "$i" -lt 4 ] && sleep 10
  done
  /usr/bin/awk -v a="$s" 'BEGIN { printf "%.0f", a / 4 }'
}
LOGIC="閉じていた"
if [ "$NIGHTLY" -eq 1 ]; then
  WAITED=0
  while :; do
    CPU="$(logic_cpu)"
    [ -z "$CPU" ] && { LOGIC="閉じていた"; break; }
    if [ "$CPU" -lt 20 ]; then LOGIC="開いていたが止まっていた（CPU ${CPU} %）"; break; fi
    if [ "$WAITED" -ge 14400 ]; then
      say "SKIPPED=Logic が 4 時間ずっと鳴っていた（CPU ${CPU} %）ので、今回は検査しませんでした"
      write_status "STATUS=skipped" "REASON=Logic が 4 時間ずっと鳴っていた（再生・録音中。CPU ${CPU} %）ので、今回は検査しませんでした"
      exit 0
    fi
    [ "$WAITED" -eq 0 ] && say "Logic が鳴っている（CPU ${CPU} %）ので、止まるまで待ちます"
    sleep 120; WAITED=$((WAITED + 150))
  done
fi

# 前回 = 全部を測った回（一部だけの回は飛ばす）
PREV=""
for d in $(ls -d "$OUT"/runs/*/ 2>/dev/null | sort -r); do
  [ -f "${d}FULL" ] && { PREV="${d%/}"; break; }
done
RUN="$OUT/runs/$(date +%Y-%m-%d-%H%M)"
[ -e "$RUN" ] && RUN="$RUN-$(date +%S)"
START=$(date +%s)
[ "$NIGHTLY" -eq 1 ] && say "検査を始めます（Logic: $LOGIC）"
# 検査の間は Mac をスリープさせない
/usr/bin/caffeinate -i "$BIN" all "$RUN" --repo "$REPO" ${ARGS[@]+"${ARGS[@]}"} 2> "$RUN.log"
ln -sfn "$RUN" "$OUT/latest"
[ "$FULL" -eq 1 ] && [ -f "$RUN/summary.md" ] && touch "$RUN/FULL"
MIN=$(( ($(date +%s) - START) / 60 ))
LINE="$(grep -m1 "異常あり" "$RUN/summary.md" 2>/dev/null)"

echo "RUN=$RUN"
echo "PREV=$PREV"
echo "MINUTES=$MIN"
[ -n "$LINE" ] && echo "$LINE"

if [ "$NIGHTLY" -eq 1 ]; then
  if [ -f "$RUN/summary.md" ]; then
    touch "$RUN/NIGHTLY"; ln -sfn "$RUN" "$OUT/nightly"
    write_status "STATUS=done" "RUN=$RUN" "PREV=$PREV" "MINUTES=$MIN" "LOGIC=$LOGIC" "SUMMARY=$LINE"
  else
    write_status "STATUS=failed" "RUN=$RUN" "REASON=summary.md ができませんでした（$RUN.log の最後: $(tail -1 "$RUN.log" 2>/dev/null)）"
  fi
fi
exit 0
