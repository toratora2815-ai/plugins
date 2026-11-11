#!/bin/bash
# 自動再開のフックを入れる（auto_resume.py を直したときの入れ直しも同じ）。
# plugins リポジトリのフォルダで: bash tools/auto-resume/install.sh
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
DIR="$HOME/.claude/auto-resume"
S="$HOME/.claude/settings.json"
mkdir -p "$DIR/state"
cp "$SRC/auto_resume.py" "$DIR/auto_resume.py"
[ -f "$S" ] && cp "$S" "$S.backup-auto-resume"

# ~/.claude/settings.json の Stop と StopFailure に、裏で待つフック（asyncRewake）を足す。ほかの設定はそのまま
/usr/bin/python3 -I - "$S" "$DIR/auto_resume.py" <<'PY'
import json, os, sys
path, script = sys.argv[1], sys.argv[2]
try:
    with open(path, encoding="utf-8") as f:
        s = json.load(f)
except FileNotFoundError:
    s = {}
# 本体がないときは何もしない。本体が「起こす」（終了コード 75）と言ったときだけ 2 を返す（asyncRewake の合図）。
# python 自体の失敗（ファイルが開けないと 2 になる）で誤ってセッションを起こさないための読み替え
cmd = 'f="%s"; [ -f "$f" ] || exit 0; /usr/bin/python3 -I "$f"; [ $? -eq 75 ] && exit 2; exit 0' % script
hook = {"type": "command", "command": cmd,
        "asyncRewake": True, "timeout": 604800,
        "rewakeMessage": "[自動再開]", "rewakeSummary": "制限が解除されたので自動で再開"}
hooks = s.setdefault("hooks", {})
for ev in ("Stop", "StopFailure"):
    groups = hooks.setdefault(ev, [])
    for g in groups:  # 前に入れた自動再開のフックは外してから入れ直す
        g["hooks"] = [h for h in g.get("hooks", []) if "auto_resume.py" not in str(h.get("command", ""))]
    groups[:] = [g for g in groups if g.get("hooks")]
    groups.append({"hooks": [dict(hook)]})
with open(path + ".tmp", "w", encoding="utf-8") as f:
    json.dump(s, f, ensure_ascii=False, indent=2)
    f.write("\n")
os.replace(path + ".tmp", path)
PY

echo "入りました。"
echo "・使用制限で止まったセッションは、制限が解除されたら自動で続きをやります"
echo "・様子: cat ~/.claude/auto-resume/log.txt"
echo "・外す: bash tools/auto-resume/uninstall.sh"
