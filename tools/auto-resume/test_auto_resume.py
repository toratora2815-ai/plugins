# auto_resume.py のテスト（作り物の記録で、待ち時間を短くして通しで確かめる）
# 実行: /usr/bin/python3 -I tools/auto-resume/test_auto_resume.py
import importlib.util, json, os, subprocess, sys, tempfile, time, datetime as dt

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "auto_resume.py")
spec = importlib.util.spec_from_file_location("ar", SCRIPT)
ar = importlib.util.module_from_spec(spec); spec.loader.exec_module(ar)
from zoneinfo import ZoneInfo
JST = ZoneInfo("Asia/Tokyo")
fails = 0
def check(name, cond, extra=""):
    global fails
    print(("OK   " if cond else "FAIL ") + name + ("  " + str(extra) if extra else ""))
    if not cond: fails += 1
def jst(y, mo, d, h, mi):
    return dt.datetime(y, mo, d, h, mi, tzinfo=JST).timestamp()
def show(t):
    return dt.datetime.fromtimestamp(t, JST).strftime("%m/%d %H:%M") if t else None

# 1) 解除時刻の読み取り
T = "You've hit your 5-hour limit · resets 3:30am (Asia/Tokyo)"
r = ar.parse_reset(T, jst(2026, 11, 12, 1, 32)); check("01:32 に 3:30am → 同じ日 03:30", r == jst(2026, 11, 12, 3, 30), show(r))
r = ar.parse_reset(T, jst(2026, 11, 12, 3, 31)); check("03:31 に 3:30am → 同じ日（過ぎたばかり）", r == jst(2026, 11, 12, 3, 30), show(r))
r = ar.parse_reset(T, jst(2026, 11, 12, 4, 0)); check("04:00 に 3:30am → 次の日", r == jst(2026, 11, 13, 3, 30), show(r))
r = ar.parse_reset(T, jst(2026, 11, 12, 23, 50)); check("23:50 に 3:30am → 次の日", r == jst(2026, 11, 13, 3, 30), show(r))
r = ar.parse_reset("You've hit your weekly limit · resets Nov 18, 1pm (Asia/Tokyo)", jst(2026, 11, 12, 1, 0)); check("週の制限 Nov 18, 1pm", r == jst(2026, 11, 18, 13, 0), show(r))
r = ar.parse_reset("resets Jan 2, 9am (Asia/Tokyo)", jst(2026, 12, 30, 10, 0)); check("年またぎ Jan 2", r == jst(2027, 1, 2, 9, 0), show(r))
r = ar.parse_reset("limit · resets 12am (Asia/Tokyo)", jst(2026, 11, 12, 20, 0)); check("12am → 次の日 0:00", r == jst(2026, 11, 13, 0, 0), show(r))
r = ar.parse_reset("limit · resets 12pm (Asia/Tokyo)", jst(2026, 11, 12, 8, 0)); check("12pm → 12:00", r == jst(2026, 11, 12, 12, 0), show(r))
w = time.time(); r = ar.parse_reset("resets in 2h 5m", w); check("resets in 2h 5m", abs(r - (w + 7500)) < 1)
check("時刻の書いていない文 → わからない", ar.parse_reset("API Error: 429 rate limited", w) is None)

# 3) フックとして通しで動かす（作り物の記録、待ち時間は短く）
tmp = tempfile.mkdtemp(prefix="ar-")
projects = os.path.join(tmp, "projects", "p"); os.makedirs(projects)
env = dict(os.environ, AUTO_RESUME_DIR=os.path.join(tmp, "base"), AUTO_RESUME_PROJECTS=os.path.join(tmp, "projects"), AUTO_RESUME_FAST="1")
def iso(t): return dt.datetime.fromtimestamp(t, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
def write_tr(path, rows):
    with open(path, "w") as f:
        for r in rows: f.write(json.dumps(r, ensure_ascii=False) + "\n")
def user(text, t, meta=False, u=None): return {"type": "user", "isMeta": meta, "uuid": u or "u%f" % t, "timestamp": iso(t), "message": {"role": "user", "content": text}}
def asst(text, t, err=False):
    d = {"type": "assistant", "uuid": "a%f" % t, "timestamp": iso(t), "message": {"role": "assistant", "content": [{"type": "text", "text": text}]}}
    if err: d.update(isApiErrorMessage=True, error="rate_limit")
    return d
def run(payload, wait=True):
    p = subprocess.Popen([sys.executable, "-I", SCRIPT], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
    p.stdin.write(json.dumps(payload)); p.stdin.close()
    if not wait: return p
    out = p.stderr.read(); p.wait(); return p.returncode, out

now = time.time()
tr = os.path.join(projects, "s1.jsonl")
write_tr(tr, [user("作って", now - 60), asst("作業中", now - 50), asst("You've hit your 5-hour limit · resets in 0m", now - 1, err=True)])
base = {"session_id": "s1", "transcript_path": tr, "cwd": "/x"}
t = time.time(); code, out = run(dict(base, hook_event_name="StopFailure", error="rate_limit", error_details="You've hit your 5-hour limit · resets in 0m"))
check("制限エラー → 少し待って起こす（終了コード 75）", code == 75 and "使用制限が解除された" in out, "code=%s %.1fs" % (code, time.time() - t))
code, out = run(dict(base, hook_event_name="StopFailure", error="rate_limit"))
check("同じエラーでもう一度 → 起こさない", code == 0)
code, out = run(dict(base, hook_event_name="StopFailure", error="server_error", error_details="Overloaded"))
check("制限ではないエラー → 何もしない", code == 0)

tr2 = os.path.join(projects, "s2.jsonl")
write_tr(tr2, [user("作って", now - 60), user("[Usage limit reached; a short grace allowance remains, ...]", now - 30, meta=True, u="g1"), asst("上限に達したので、ここでいったん止めます", now - 5)])
other = os.path.join(projects, "s3.jsonl")
write_tr(other, [asst("You've hit your 5-hour limit · resets in 0m", time.time() + 3, err=True)])
t = time.time(); code, out = run({"session_id": "s2", "transcript_path": tr2, "hook_event_name": "Stop"})
check("区切りよく止めた → ほかのセッションの時刻で起こす", code == 75 and "使用制限が解除された" in out, "code=%s %.1fs" % (code, time.time() - t))
code, out = run({"session_id": "s2", "transcript_path": tr2, "hook_event_name": "Stop"})
check("同じ合図でもう一度 → 起こさない", code == 0)

os.remove(other)
tr4 = os.path.join(projects, "s4.jsonl")
write_tr(tr4, [user("作って", now - 60), user("[Usage limit reached; a short grace allowance remains, ...]", now - 30, meta=True, u="g4"), asst("止めます", now - 5)])
t = time.time(); code, out = run({"session_id": "s4", "transcript_path": tr4, "hook_event_name": "Stop"})
check("時刻がわからない → いったん起こす（15 分のところ、テストでは 4 秒）", code == 75 and "15 分" in out, "%.1fs" % (time.time() - t))

tr5 = os.path.join(projects, "s5.jsonl")
write_tr(tr5, [user("作って", now - 60), user("[Usage limit reached; ...]", now - 30, meta=True, u="g5"), asst("止めます", now - 5)])
p = run({"session_id": "s5", "transcript_path": tr5, "hook_event_name": "Stop"}, wait=False)
time.sleep(1.5)
with open(tr5, "a") as f: f.write(json.dumps(user("続けて", time.time())) + "\n")
p.wait(); check("待っている間に本人が話しかけた → 起こさない", p.returncode == 0, "code=%s" % p.returncode)

tr6 = os.path.join(projects, "s6.jsonl")
write_tr(tr6, [user("作って", now - 60), user("[Usage limit reached; ...]", now - 30, meta=True, u="g6"), asst("止めます", now - 5)])
pa = run({"session_id": "s6", "transcript_path": tr6, "hook_event_name": "Stop"}, wait=False)
time.sleep(1.0)
code, out = run({"session_id": "s6", "transcript_path": tr6, "hook_event_name": "StopFailure", "error": "rate_limit", "error_details": "resets in 0m"})
pa.wait()
check("新しい止まり方が来たら、前の待ちは引き継いで終わる（起こすのは 1 回）", pa.returncode == 0 and code == 75, "前=%s 後=%s" % (pa.returncode, code))

tr10 = os.path.join(projects, "s10.jsonl")
write_tr(tr10, [user("作って", now - 3600), user("[Usage limit reached; ...]", now - 2400, meta=True, u="g10"), asst("止めます", now - 2390),
                user("Another Claude session sent a message: 続けて", now - 300, meta=True), asst("続きをやりました", now - 10)])
code, out = run({"session_id": "s10", "transcript_path": tr10, "hook_event_name": "Stop"})
check("40 分前の合図のあと、別セッションの知らせで動いて終わった → 起こさない", code == 0)

code, out = run({"session_id": "s7", "transcript_path": tr, "hook_event_name": "Stop"})
check("ふつうに終わったターン（合図なし）→ 何もしない", code == 0)
t = time.time(); code, out = run({"session_id": "s8", "transcript_path": "/nonexistent", "hook_event_name": "Stop"})
check("記録がない → 何もしない（すぐ終わる）", code == 0 and time.time() - t < 2)

with open(os.path.join(tmp, "base", "selftest"), "w") as f: f.write("s9 2")
t = time.time(); code, out = run({"session_id": "s9", "transcript_path": tr, "hook_event_name": "Stop"})
check("動作確認（selftest）→ 2 秒後に起こす", code == 75 and "selftest" in out, "%.1fs" % (time.time() - t))

print("--- log"); print(open(os.path.join(tmp, "base", "log.txt")).read())
print("FAILED", fails) if fails else print("ALL PASSED")

# 4) フックの命令（読み替えのラッパー）
def wrap(path):
    return 'f="%s"; [ -f "$f" ] || exit 0; /usr/bin/python3 -I "$f"; [ $? -eq 75 ] && exit 2; exit 0' % path
def run_wrap(path, payload):
    p = subprocess.run(["/bin/zsh", "-c", wrap(path)], input=json.dumps(payload), capture_output=True, text=True, env=env)
    return p.returncode, p.stderr
with open(os.path.join(tmp, "base", "selftest"), "w") as f: f.write("w1 1")
code, err = run_wrap(SCRIPT, {"session_id": "w1", "transcript_path": tr, "hook_event_name": "Stop"})
check("命令経由: 起こすとき → 2（Claude に届く）", code == 2 and "selftest" in err, code)
code, err = run_wrap(SCRIPT, {"session_id": "w2", "transcript_path": tr, "hook_event_name": "Stop"})
check("命令経由: 何もないとき → 0", code == 0, code)
code, err = run_wrap("/nonexistent/auto_resume.py", {"session_id": "w3", "hook_event_name": "Stop"})
check("命令経由: 本体がない → 0（誤って起こさない）", code == 0, code)
bad = os.path.join(tmp, "bad.py"); open(bad, "w").write("import sys\nsys.exit(2)\n")
code, err = run_wrap(bad, {"hook_event_name": "Stop"})
check("命令経由: 本体が 2 で失敗 → 0（誤って起こさない）", code == 0, code)
print("FAILED", fails) if fails else print("ALL PASSED (wrapper)")
