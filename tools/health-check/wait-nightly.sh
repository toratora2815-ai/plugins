#!/bin/bash
# 報告係（Claude アプリの定期実行）用: 夜の測定（nightly.sh --nightly）が動いていれば、終わるまで待つ（最大 90 分）。
# 決まった形のコマンドにして、許可の確認で止まらないようにしてある（.claude/settings.local.json で許可）
#   使い方: bash tools/health-check/wait-nightly.sh   （run_in_background で動かして、終わりの通知を待つ）
for i in $(seq 1 90); do
  pgrep -f "nightly.sh --nightly" >/dev/null || { echo "測定は終わっています"; exit 0; }
  sleep 60
done
echo "90 分たっても測定が終わりません"
exit 1
