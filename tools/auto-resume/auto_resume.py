#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 自動再開フック … 使用制限で止まったセッションを、制限が解除されたら起こして続きをやらせる。
# （2026-11-12 本人の指示「今実行中のプラグイン作りとかは、制限が来たら、制限が解除された時に自動的に続きをやるようにしてください」）
#
# Claude Code のフック（~/.claude/settings.json の Stop と StopFailure、asyncRewake: true）として、裏で動く。
#   StopFailure … 制限エラー（rate_limit）でターンが切れたとき。エラー文「… resets 3:30am (Asia/Tokyo)」から解除の時刻を読む
#   Stop        … 「制限まで残りわずか」の合図（[Usage limit reached; …]）を受けて、区切りよく止めたとき。
#                  時刻は、ほかのセッションの記録に残った制限エラーから読む。見つからなければ 15 分後にいったん起こす
#                  （まだ制限中なら、すぐ制限エラーで止まり、StopFailure 側が正しい時刻まで待ち直す。断られた分は利用枠を使わない）
# 時刻が来たら終了コード 75 で終わる → フックの命令が 2 に読み替え → 標準エラーに書いた文が Claude に届いて、ターンが始まる。
# Claude のアプリ自身にも自動再開があるが、「制限エラーで途中で切れた」セッションの一部だけ（区切りよく止めたものは対象外）。
# 切れたものはアプリに先を譲り（解除＋2 分半まで待つ）、まだ動いていなければこちらが起こす。
# 待っている間に本人が話しかけた・ほかの理由で動き出したときは、起こさずに終わる。
# 記録は ~/.claude/auto-resume/log.txt。元のファイルは plugins リポジトリの tools/auto-resume/。

import datetime as dt
import json
import os
import re
import signal
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
BASE = os.environ.get("AUTO_RESUME_DIR") or os.path.join(HOME, ".claude", "auto-resume")
STATE = os.path.join(BASE, "state")
LOG = os.path.join(BASE, "log.txt")
PROJECTS = os.environ.get("AUTO_RESUME_PROJECTS") or os.path.join(HOME, ".claude", "projects")

GRACE_MARK = "[Usage limit reached;"
MARGIN = 90                # 区切りよく止めたもの: 解除の時刻から、さらに待つ秒数（時計のずれ対策）
MARGIN_HARD = 150          # 制限エラーで切れたもの: Claude のアプリ自身の自動再開（解除＋約 1 分半）に先を譲り、
                           # それでも動いていなければ起こす（二重に「続けて」を送らない）
MIN_WAIT = 60              # 少なくともこれだけは待つ
FALLBACK_WAIT = 15 * 60    # 解除の時刻がわからないとき、いったん起こすまで
KEEP_AWAKE_MAX = 6 * 3600  # これ以下の待ちなら、待つ間 Mac を眠らせない（caffeinate -i。ふたを閉じたら眠る）
POLL = 30                  # 何秒ごとに様子を見るか
WAKE = 75                  # 起こすときの終了コード。フックの命令がこれを 2（asyncRewake の合図）に読み替える。
                           # python 自体の失敗（終了コード 1 や 2）で誤って起こさないため
if os.environ.get("AUTO_RESUME_FAST") == "1":  # テスト用（待ち時間を短くする）
    MARGIN, MARGIN_HARD, MIN_WAIT, FALLBACK_WAIT, POLL, KEEP_AWAKE_MAX = 1, 1, 2, 4, 1, 0

MESSAGE = (
    "使用制限が解除されたので、自動で起こしました（本人の指示「今実行中のプラグイン作りとかは、制限が来たら、"
    "制限が解除された時に自動的に続きをやるようにしてください」2026-11-12 による自動再開）。"
    "制限で止まったところから、作業の続きをやってください。本人に決めてもらうことが出てきたら、そこで止めて聞いてください。"
    "もう終わっていたら、ひとこと報告するだけで大丈夫です。"
)
FALLBACK_MESSAGE = (
    "使用制限で止まってから 15 分たったので、いったん自動で起こしました（本人の指示による自動再開。解除の時刻がわからなかったため）。"
    "制限で止まったところから、作業の続きをやってください。まだ制限中なら、すぐ止まって、解除の時刻にまた自動で起こされます。"
    "本人に決めてもらうことが出てきたら、そこで止めて聞いてください。もう終わっていたら、ひとこと報告するだけで大丈夫です。"
)
TEST_MESSAGE = "これは自動再開の仕組みの動作確認（selftest）です。届いたら、そのことを確かめて、動作確認の続きをやってください。"


def log(msg):
    try:
        os.makedirs(BASE, exist_ok=True)
        if os.path.exists(LOG) and os.path.getsize(LOG) > 256 * 1024:
            with open(LOG, "r", encoding="utf-8", errors="replace") as f:
                keep = f.read()[-128 * 1024:]
            with open(LOG, "w", encoding="utf-8") as f:
                f.write(keep)
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg + "\n")
    except Exception:
        pass


def hm(t):
    return time.strftime("%m/%d %H:%M", time.localtime(t))


# ---------- 会話の記録（transcript、1 行 1 JSON）を読む ----------

def tail_lines(path, max_bytes=4 * 1024 * 1024, start=None):
    """ファイルの終わりのほう（start があればそこから）を行に分けて返す。"""
    try:
        size = os.path.getsize(path)
        begin = max(0, size - max_bytes) if start is None else max(start, size - max_bytes)
        with open(path, "rb") as f:
            f.seek(begin)
            data = f.read()
    except (OSError, TypeError):
        return []
    lines = data.decode("utf-8", "replace").split("\n")
    if begin > 0 and begin != start:
        lines = lines[1:]  # 途中から読んだ最初の行は切れているので捨てる
    return [l for l in lines if l.strip()]


def load(line):
    try:
        d = json.loads(line)
        return d if isinstance(d, dict) else None
    except Exception:
        return None


def text_of(d):
    m = d.get("message")
    if not isinstance(m, dict):
        return ""
    c = m.get("content")
    if isinstance(c, str):
        return c
    out = []
    if isinstance(c, list):
        for b in c:
            if isinstance(b, dict) and b.get("type") == "text":
                out.append(str(b.get("text", "")))
    return "\n".join(out)


def is_turn_start(d):
    """本人（や別セッション・通知）の発言でターンが始まった行か。道具の結果だけの行は数えない。"""
    if d.get("type") != "user" or d.get("isMeta") or d.get("isSidechain"):
        return False
    m = d.get("message")
    c = m.get("content") if isinstance(m, dict) else None
    if isinstance(c, list) and any(isinstance(b, dict) and b.get("type") == "tool_result" for b in c):
        return False
    return bool(text_of(d).strip())


GRACE_FRESH = 30 * 60  # 合図からこれ以上たって止まったものは、前のターンの合図とみなす


def find_grace(lines, now):
    """いまのターンに「制限まで残りわずか」の合図があれば、その行の id を返す。
    合図は止まる数分前に来るので、30 分より古い合図は数えない（別セッションの知らせで動いた後のターンなどを誤って起こさない）。"""
    for line in reversed(lines):
        if '"user"' not in line:
            continue
        d = load(line)
        if not d:
            continue
        if d.get("type") == "user" and d.get("isMeta") and GRACE_MARK in text_of(d)[:300]:
            ts = iso_ts(d.get("timestamp"))
            if ts is not None and ts < now - GRACE_FRESH:
                return None
            return str(d.get("uuid") or d.get("timestamp") or "")
        if is_turn_start(d):
            return None
    return None


def find_limit_error(lines):
    for line in reversed(lines):
        if '"rate_limit"' not in line:
            continue
        d = load(line)
        if d and d.get("isApiErrorMessage") and d.get("error") == "rate_limit":
            return d
    return None


def iso_ts(s):
    try:
        return dt.datetime.fromisoformat(str(s).replace("Z", "+00:00")).timestamp()
    except Exception:
        return None


# ---------- 解除の時刻を読む ----------

MONTHS = {m: i for i, m in enumerate(
    ["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"], 1)}
RESET_AT = re.compile(
    r"resets\s+(?:at\s+|on\s+)?"
    r"(?:(?P<mon>[A-Za-z]{3})[a-z]*\.?\s+(?P<day>\d{1,2})(?:st|nd|rd|th)?,?\s+(?:at\s+)?)?"
    r"(?P<h>\d{1,2})(?::(?P<m>\d{2}))?\s*(?P<ap>[ap]\.?m\.?)?"
    r"(?:\s*\((?P<tz>[A-Za-z_]+(?:/[A-Za-z0-9_+\-]+)+|UTC|GMT)\))?", re.I)
RESET_IN = re.compile(
    r"resets\s+in\s+(?:(?P<d>\d+)\s*d[a-z]*\s*)?(?:(?P<h>\d+)\s*h[a-z]*\s*)?(?:(?P<m>\d+)\s*m[a-z]*)?", re.I)


def parse_reset(text, when):
    """「resets 3:30am (Asia/Tokyo)」「resets Nov 18, 1pm (Asia/Tokyo)」「resets in 2h 5m」→ 秒（when はその文が出た時刻）。"""
    if not text:
        return None
    m = RESET_IN.search(text)
    if m and any(m.group(k) for k in ("d", "h", "m")):
        return when + int(m.group("d") or 0) * 86400 + int(m.group("h") or 0) * 3600 + int(m.group("m") or 0) * 60
    m = RESET_AT.search(text)
    if not m:
        return None
    tz = None
    if m.group("tz"):
        try:
            from zoneinfo import ZoneInfo
            tz = ZoneInfo("UTC" if m.group("tz").upper() == "GMT" else m.group("tz"))
        except Exception:
            tz = None
    base = dt.datetime.fromtimestamp(when, tz) if tz else dt.datetime.fromtimestamp(when)
    h, mi = int(m.group("h")), int(m.group("m") or 0)
    ap = (m.group("ap") or "").lower().replace(".", "")
    if ap == "pm" and h < 12:
        h += 12
    if ap == "am" and h == 12:
        h = 0
    if h > 23 or mi > 59:
        return None
    try:
        if m.group("mon"):
            mon = MONTHS.get(m.group("mon")[:3].lower())
            if not mon:
                return None
            cand = base.replace(month=mon, day=int(m.group("day")), hour=h, minute=mi, second=0, microsecond=0)
            if cand.timestamp() < when - 30 * 86400:  # 年をまたいだ
                cand = cand.replace(year=cand.year + 1)
        else:
            cand = base.replace(hour=h, minute=mi, second=0, microsecond=0)
            if cand.timestamp() < when - 20 * 60:     # もう過ぎた時刻 → 次の日
                cand = cand + dt.timedelta(days=1)
    except ValueError:
        return None
    return cand.timestamp()


def reset_from_other_sessions(now):
    """5.5 時間以内に書かれたほかのセッションの記録から、いちばん新しい制限エラーの、まだ来ていない解除時刻を探す。"""
    files = []
    try:
        for d in os.listdir(PROJECTS):
            p = os.path.join(PROJECTS, d)
            if not os.path.isdir(p):
                continue
            for f in os.listdir(p):
                if f.endswith(".jsonl"):
                    fp = os.path.join(p, f)
                    try:
                        mt = os.path.getmtime(fp)
                    except OSError:
                        continue
                    if mt >= now - 5.5 * 3600:
                        files.append((mt, fp))
    except OSError:
        return None
    best = None  # (エラーの時刻, 解除の時刻)
    for mt, fp in sorted(files, reverse=True)[:60]:
        d = find_limit_error(tail_lines(fp, 1024 * 1024))
        if not d:
            continue
        when = iso_ts(d.get("timestamp")) or mt
        if when < now - 5.5 * 3600:
            continue
        r = parse_reset(text_of(d), when)
        if r and r > now and (best is None or when > best[0]):
            best = (when, r)
    return best[1] if best else None


# ---------- 待つ ----------

def ps(pid, field):
    try:
        return subprocess.run(["/bin/ps", "-p", str(pid), "-o", field + "="],
                              capture_output=True, text=True, timeout=5).stdout.strip()
    except Exception:
        return ""


def find_claude_pid():
    """このフックを動かしている Claude（セッション）のプロセスを探す。"""
    pid = os.getppid()
    for _ in range(5):
        if pid <= 1:
            return None
        cmd = ps(pid, "command")
        first = cmd.split(" --", 1)[0]
        if first.endswith("/claude") or first == "claude" or "/MacOS/claude" in first:
            return pid
        try:
            pid = int(ps(pid, "ppid") or 0)
        except ValueError:
            return None
    return None


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def take_over(sid):
    """同じセッションで前から待っている自分がいたら止めて、こちらが引き継ぐ（新しい情報のほうが正しい）。"""
    pf = os.path.join(STATE, sid + ".pid")
    try:
        old = int(open(pf).read().strip())
        if old != os.getpid() and alive(old) and "auto_resume.py" in ps(old, "command"):
            os.kill(old, signal.SIGTERM)
    except Exception:
        pass
    try:
        with open(pf, "w") as f:
            f.write(str(os.getpid()))
    except OSError:
        pass


def release(sid):
    pf = os.path.join(STATE, sid + ".pid")
    try:
        if open(pf).read().strip() == str(os.getpid()):
            os.remove(pf)
    except Exception:
        pass


def moved_on(tpath, since_size, t0):
    """待ち始めてから、そのセッションが自分で動き出したか（本人が話しかけた・返事が書かれた）。"""
    if not tpath:
        return False
    try:
        if os.path.getsize(tpath) <= since_size:
            return False
    except OSError:
        return False
    for line in tail_lines(tpath, 2 * 1024 * 1024, start=since_size):
        d = load(line)
        if not d or d.get("isSidechain"):
            continue
        ts = iso_ts(d.get("timestamp"))
        if ts is None or ts <= t0 + 1:
            continue
        if d.get("type") == "assistant" and not d.get("isApiErrorMessage"):
            return True
        if is_turn_start(d):
            return True
    return False


def wait_until(target, tpath, since_size, t0, claude_pid):
    caf = None
    if target - time.time() <= KEEP_AWAKE_MAX:
        try:
            caf = subprocess.Popen(["/usr/bin/caffeinate", "-i", "-w", str(os.getpid())],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            caf = None
    try:
        while True:
            now = time.time()
            if now >= target:
                return "ok"
            time.sleep(min(POLL, max(1.0, target - now)))
            if claude_pid and not alive(claude_pid):
                return "セッションが閉じられた"
            if moved_on(tpath, since_size, t0):
                return "セッションがもう動き出した"
    finally:
        if caf:
            try:
                caf.terminate()
            except Exception:
                pass


# ---------- 状態（同じ止まり方で 2 回起こさない） ----------

def load_state(sid):
    try:
        with open(os.path.join(STATE, sid + ".json"), encoding="utf-8") as f:
            s = json.load(f)
            if isinstance(s, dict) and isinstance(s.get("handled"), list):
                return s
    except Exception:
        pass
    return {"handled": []}


def save_state(sid, s):
    try:
        p = os.path.join(STATE, sid + ".json")
        with open(p + ".tmp", "w", encoding="utf-8") as f:
            json.dump(s, f, ensure_ascii=False)
        os.replace(p + ".tmp", p)
    except Exception:
        pass


def main():
    t0 = time.time()
    raw = sys.stdin.read()
    try:
        data = json.loads(raw) if raw.strip() else {}
    except Exception:
        data = {}
    event = str(data.get("hook_event_name") or "")
    sid = re.sub(r"[^A-Za-z0-9_.-]", "_", str(data.get("session_id") or "unknown"))
    tpath = str(data.get("transcript_path") or "")
    os.makedirs(STATE, exist_ok=True)
    lines = tail_lines(tpath) if tpath else []

    message, reset, key, why = MESSAGE, None, None, ""
    selftest = os.path.join(BASE, "selftest")
    st_parts = []
    if event == "Stop" and os.path.exists(selftest):
        try:
            st_parts = open(selftest, encoding="utf-8").read().split()
        except OSError:
            st_parts = []
    if st_parts and st_parts[0] == sid:
        os.remove(selftest)
        secs = int(st_parts[1]) if len(st_parts) > 1 and st_parts[1].isdigit() else 20
        key, reset, why, message = "selftest-%d" % t0, t0 + secs - MARGIN, "動作確認（%d 秒後に起こす）" % secs, TEST_MESSAGE
    elif event == "StopFailure":
        err = str(data.get("error") or "")
        details = " ".join(str(data.get(k) or "") for k in ("error_details", "last_assistant_message"))
        e = find_limit_error(lines[-300:])
        if e and (iso_ts(e.get("timestamp")) or 0) < t0 - 120:
            e = None  # 前に制限になったときのエラー（今回の分はまだ書かれていない）
        if e:
            details += " " + text_of(e)
        if err != "rate_limit" and not re.search(r"hit your .*limit|usage limit", details, re.I):
            return 0
        key = "fail-" + str((e or {}).get("uuid") or int(t0 // 60))
        reset = parse_reset(details, iso_ts((e or {}).get("timestamp")) or t0)
        why = "制限エラーで停止"
    elif event == "Stop":
        g = find_grace(lines, t0)
        if not g:
            return 0
        key = "grace-" + g
        reset = reset_from_other_sessions(t0)
        why = "制限の合図を受けて区切りよく停止"
    else:
        return 0

    state = load_state(sid)
    if key in state["handled"]:
        return 0
    state["handled"] = (state["handled"] + [key])[-50:]
    save_state(sid, state)

    if reset:
        if key.startswith("selftest"):
            target = reset + MARGIN
        else:
            target = max(reset + (MARGIN_HARD if key.startswith("fail-") else MARGIN), t0 + MIN_WAIT)
    else:
        target, message = t0 + FALLBACK_WAIT, FALLBACK_MESSAGE
    signal.signal(signal.SIGTERM, lambda *_: (log("[%s] 新しい止まり方の待ちに引き継いだので、こちらは終了" % sid[:8]), os._exit(0)))
    take_over(sid)
    log("[%s] %s → %s に起こします%s" % (sid[:8], why, hm(target), "" if reset else "（解除の時刻がわからないので、いったん）"))

    try:
        since = max(0, os.path.getsize(tpath) - 256 * 1024) if tpath else 0
    except OSError:
        since = 0
    result = wait_until(target, tpath, since, t0, find_claude_pid())
    release(sid)
    if result != "ok":
        log("[%s] 起こさずに終了（%s）" % (sid[:8], result))
        return 0
    log("[%s] 起こしました" % sid[:8])
    sys.stderr.write(message + "\n")
    sys.stderr.flush()
    return WAKE


if __name__ == "__main__":
    try:
        code = main()
    except Exception as ex:  # どんなときも、セッションの邪魔はしない
        log("エラー: %r" % (ex,))
        code = 0
    sys.exit(code)
