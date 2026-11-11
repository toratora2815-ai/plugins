#!/bin/bash
# 毎晩の健康診断（数値の検査まで）。Claude アプリの定期実行「【毎晩】自作プラグイン健康診断」から呼ばれる。
# 手で動かしてもよい（~/plugins で）:
#   bash tools/health-check/nightly.sh                 入っている自作 AU を全部
#   bash tools/health-check/nightly.sh --only Ecdr,Mytr   一部だけ（コードかフォルダ名）
#   bash tools/health-check/nightly.sh --groups basic,click --skip-auval   検査を選ぶ
# 結果: build/health/runs/<日時>/summary.md（一覧）・summary.json・raw/（細かい数値と記録）。最新は build/health/latest
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO/tools/health-check/hc.cpp"
OUT="$REPO/build/health"
BIN="$OUT/hc"
mkdir -p "$OUT/runs" "$OUT/reports"

# 検査ツールを作る（ソースのほうが新しいときだけ）
if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  /usr/bin/clang++ -std=c++17 -O2 -Wno-deprecated-declarations -o "$BIN" "$SRC" \
    -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreFoundation \
    || { echo "検査ツール（hc）のビルドに失敗しました"; exit 1; }
fi

# Logic が開いている間は待つ（夜中に Mac が寝ていて、起こした作業中に動き出したとき、録音と CPU を取り合わないように）
logic_open() { /usr/bin/pgrep -xq "Logic Pro" || /usr/bin/pgrep -xq "Logic Pro X"; }
WAITED=0
while logic_open; do
  if [ "$WAITED" -ge 28800 ]; then echo "SKIPPED=Logic が 8 時間開いたままだったので、今回は検査しませんでした"; exit 0; fi
  [ "$WAITED" -eq 0 ] && echo "Logic が開いているので、閉じるまで待ちます"
  sleep 120; WAITED=$((WAITED + 120))
done

PREV="$(ls -d "$OUT"/runs/*/ 2>/dev/null | sort | tail -1)"
RUN="$OUT/runs/$(date +%Y-%m-%d-%H%M)"
START=$(date +%s)
# 検査の間は Mac をスリープさせない
/usr/bin/caffeinate -i "$BIN" all "$RUN" --repo "$REPO" "$@" 2> "$RUN.log"
ln -sfn "$RUN" "$OUT/latest"

echo "RUN=$RUN"
echo "PREV=${PREV%/}"
echo "MINUTES=$(( ($(date +%s) - START) / 60 ))"
grep -m1 "異常あり" "$RUN/summary.md" 2>/dev/null
