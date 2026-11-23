#!/usr/bin/env python3
"""Linux integration test for healthd v1 (no QNX needed).

Builds the Linux binaries, runs healthd in the foreground against a
test config that supervises:
  - crashdemo (dies every ~2 s, must be restarted repeatedly), and
  - a stable `sleep 300` process (must stay running, 0 restarts).
Then checks the status snapshot, the events log, and the host tool
(validate / status / report).

Run:  make test        (from the repo root)
Exit code 0 = all checks passed.
"""
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "out-linux"
TMP = Path("/tmp/healthwatch-test")
STATUS = TMP / "health.json"
EVENTS = TMP / "health-events.jsonl"
REPORT = TMP / "report.html"

TEST_CONFIG = """check_interval: 1
status_file: {status}
events_file: {events}

processes:
  - name: crashdemo
    command: CRASH_AFTER=2 {crashdemo}
    restart: always
    max_restarts: 0
    restart_delay: 1
  - name: sleeper
    command: sleep 300
    restart: always
    max_restarts: 3
    restart_delay: 1
"""


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60,
                       **kw)
    return r


def check(name, cond, detail=""):
    print(f"  {'PASS' if cond else 'FAIL'}  {name}"
          + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        raise SystemExit(f"FAILED: {name} {detail}")
    return cond


def main():
    TMP.mkdir(parents=True, exist_ok=True)
    for p in (STATUS, EVENTS):
        p.unlink(missing_ok=True)
    cfg = TEST_CONFIG.format(status=STATUS, events=EVENTS,
                             crashdemo=BIN / "crashdemo")
    cfg_path = TMP / "test.yaml"
    cfg_path.write_text(cfg)

    print("== healthwatch validate ==")
    r = run([sys.executable, str(ROOT / "host/healthwatch.py"),
             "validate", str(cfg_path)])
    check("config validates", r.returncode == 0, r.stdout + r.stderr)

    print("== build ==")
    r = run(["make", "linux"], cwd=ROOT)
    check("make linux", r.returncode == 0, r.stdout[-500:] + r.stderr[-500:])

    print("== run healthd for ~20 s (crashdemo dies every ~2 s) ==")
    log = open(TMP / "healthd.log", "w")
    proc = subprocess.Popen(
        [str(BIN / "healthd"), "-c", str(cfg_path), "-f"],
        stdout=log, stderr=subprocess.STDOUT, text=True)
    # Read the status snapshot WHILE healthd is still running: after a
    # graceful shutdown healthd kills its children, so a post-shutdown
    # snapshot would (correctly) show them dead.
    time.sleep(20)
    status = json.loads(STATUS.read_text())
    events_text = EVENTS.read_text()
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    log.close()

    print("== status snapshot ==")
    check("status file exists and parses", True)
    by_name = {p["name"]: p for p in status["processes"]}
    check("crashdemo restarted at least 3 times",
          by_name["crashdemo"]["restarts"] >= 3,
          f"restarts={by_name['crashdemo']['restarts']}")
    check("crashdemo not given up: running or in backoff",
          by_name["crashdemo"]["state"] in ("running", "restarting"),
          by_name["crashdemo"]["state"])
    check("sleeper still running, 0 restarts",
          by_name["sleeper"]["state"] == "running"
          and by_name["sleeper"]["restarts"] == 0,
          str(by_name["sleeper"]))

    print("== events log ==")
    events = [json.loads(l) for l in events_text.splitlines() if l.strip()]
    kinds = {e["event"] for e in events}
    check("events include exited + restarted",
          {"exited", "restarted"} <= kinds, str(kinds))
    check("at least 3 exit events for crashdemo",
          sum(1 for e in events if e["process"] == "crashdemo"
              and e["event"] == "exited") >= 3)

    print("== host tool: status + report ==")
    r = run([sys.executable, str(ROOT / "host/healthwatch.py"), "status",
             "--status-file", str(STATUS)])
    check("healthwatch status exits 0 and shows crashdemo",
          r.returncode == 0 and "crashdemo" in r.stdout, r.stdout + r.stderr)
    r = run([sys.executable, str(ROOT / "host/healthwatch.py"), "report",
             "--status-file", str(STATUS), "--events-file", str(EVENTS),
             "--out", str(REPORT)])
    check("healthwatch report exits 0 and writes HTML",
          r.returncode == 0 and REPORT.exists(), r.stdout + r.stderr)
    check("report contains timeline + process names",
          "crashdemo" in REPORT.read_text()
          and "restarted" in REPORT.read_text())

    print("== restart-limit behaviour (max_restarts) ==")
    lim_cfg = TMP / "limit.yaml"
    lim_cfg.write_text(
        f"check_interval: 1\nstatus_file: {TMP}/lim.json\n"
        f"events_file: {TMP}/lim-events.jsonl\n\nprocesses:\n"
        f"  - name: doomed\n    command: exit 1\n"
        f"    restart: always\n    max_restarts: 2\n    restart_delay: 1\n")
    proc = subprocess.Popen(
        [str(BIN / "healthd"), "-c", str(lim_cfg), "-f"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(9)  # exit(1) instantly; backoff 1s, 2s, then exhausted
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    lim = json.loads((TMP / "lim.json").read_text())
    doomed = lim["processes"][0]
    check("max_restarts respected: exactly 2 restarts, then exhausted",
          doomed["restarts"] == 2 and doomed["state"] == "exhausted",
          str(doomed))

    print("\nALL TESTS PASSED")


if __name__ == "__main__":
    main()
