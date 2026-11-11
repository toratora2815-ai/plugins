#!/bin/bash
# 自動再開のフックを外す。plugins リポジトリのフォルダで: bash tools/auto-resume/uninstall.sh
S="$HOME/.claude/settings.json"
if [ -f "$S" ]; then
/usr/bin/python3 -I - "$S" <<'PY'
import json, os, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    s = json.load(f)
hooks = s.get("hooks", {})
for ev in list(hooks):
    groups = hooks[ev]
    for g in groups:
        g["hooks"] = [h for h in g.get("hooks", []) if "auto_resume.py" not in str(h.get("command", ""))]
    hooks[ev] = [g for g in groups if g.get("hooks")]
    if not hooks[ev]:
        del hooks[ev]
if not hooks:
    s.pop("hooks", None)
with open(path + ".tmp", "w", encoding="utf-8") as f:
    json.dump(s, f, ensure_ascii=False, indent=2)
    f.write("\n")
os.replace(path + ".tmp", path)
PY
fi
pkill -f "auto-resume/auto_resume.py" 2>/dev/null
rm -rf "$HOME/.claude/auto-resume"
echo "外しました（いま待っている分も止めました）。"
