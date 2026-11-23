#!/usr/bin/env python3
"""healthwatch — host tool for QNX HealthWatch (stdlib only).

Reads the status snapshot healthd publishes (/dev/shmem/health.json on the
target) either from a local file (for the Linux test harness, or after
`scp`) or over SSH, and renders it as a live table or an HTML report.

Usage:
    healthwatch validate config/healthwatch.yaml
    healthwatch status [--target user@host] [--status-file /dev/shmem/health.json]
    healthwatch watch  [--target user@host] [--interval 1] [--count 0]
    healthwatch report [--target user@host] [--out report.html]

No third-party dependencies: Python 3.8+ stdlib only. SSH is the
system `ssh` binary; if --target is omitted, local files are read.
"""
import argparse
import html
import json
import subprocess
import sys
import time
from datetime import datetime, timezone

VERSION = "1.0.0"
DEFAULT_STATUS = "/dev/shmem/health.json"
DEFAULT_EVENTS = "/dev/shmem/health-events.jsonl"


# ----------------------------------------------------------------------
# Input: local file or ssh cat
# ----------------------------------------------------------------------
def _ssh_cat(target, path):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o",
                        "ConnectTimeout=5", target, "cat", path],
                       capture_output=True, text=True, timeout=15)
    if r.returncode != 0:
        raise RuntimeError(f"ssh cat {path} failed: {r.stderr.strip()}")
    return r.stdout


def read_text(path, target):
    if target:
        return _ssh_cat(target, path)
    with open(path, encoding="utf-8") as f:
        return f.read()


def load_status(path, target):
    return json.loads(read_text(path, target))


def load_events(path, target):
    events = []
    try:
        text = read_text(path, target)
    except (OSError, RuntimeError):
        return events
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return events


# ----------------------------------------------------------------------
# Config validation (mirrors healthd's minimal YAML subset)
# ----------------------------------------------------------------------
def validate_config(path):
    errors, procs = [], []
    cur = None
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except OSError as e:
        return [f"cannot read {path}: {e}"], []
    for lineno, raw in enumerate(lines, 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        s = line.strip()
        if s == "processes:":
            continue
        if s.startswith("- ") and "name:" in s:
            cur = {"_line": lineno}
            procs.append(cur)
            s = s[2:].strip()
            if not s:
                continue
        if ":" not in s:
            errors.append(f"line {lineno}: no key: {s!r}")
            continue
        key, _, val = s.partition(":")
        key, val = key.strip(), val.strip().strip('"').strip("'")
        if cur is None:
            if key in ("check_interval", "status_file", "events_file"):
                continue
            errors.append(f"line {lineno}: unexpected top-level key {key!r}")
            continue
        cur[key] = val
    names = set()
    for p in procs:
        if not p.get("name"):
            errors.append(f"line {p.get('_line')}: process without name")
        elif p["name"] in names:
            errors.append(f"duplicate process name {p['name']!r}")
        names.add(p.get("name", ""))
        if not p.get("command"):
            errors.append(f"process {p.get('name')}: missing command")
        if p.get("restart", "always") not in ("always", "never"):
            errors.append(f"process {p.get('name')}: restart must be "
                          "always/never")
        for k in ("max_restarts", "restart_delay"):
            if k in p:
                try:
                    int(p[k])
                except ValueError:
                    errors.append(f"process {p.get('name')}: {k} must be int")
    if not procs:
        errors.append("no processes defined")
    return errors, procs


# ----------------------------------------------------------------------
# status / watch
# ----------------------------------------------------------------------
def fmt_table(status):
    procs = status.get("processes", [])
    rows = [("NAME", "STATE", "PID", "RESTARTS", "UPTIME")]
    for p in procs:
        rows.append((p["name"], p["state"], str(p["pid"]),
                     str(p["restarts"]), f"{p['uptime_seconds']}s"))
    widths = [max(len(r[i]) for r in rows) for i in range(5)]
    out = []
    for i, r in enumerate(rows):
        out.append("  ".join(c.ljust(w) for c, w in zip(r, widths)))
        if i == 0:
            out.append("  ".join("-" * w for w in widths))
    return "\n".join(out)


def cmd_status(args):
    try:
        status = load_status(args.status_file, args.target)
    except (OSError, RuntimeError, json.JSONDecodeError) as e:
        print(f"healthwatch: cannot read status: {e}", file=sys.stderr)
        return 1
    print(f"HealthWatch — generated {status.get('generated_at', '?')} "
          f"(daemon up {status.get('daemon_uptime_seconds', '?')}s)")
    print(fmt_table(status))
    dead = [p["name"] for p in status.get("processes", [])
            if p["state"] != "running"]
    if dead:
        print(f"ALERT: not running: {', '.join(dead)}")
    return 0


def cmd_watch(args):
    n = 0
    while args.count == 0 or n < args.count:
        print("\033[2J\033[H", end="")  # clear screen
        print(f"HealthWatch live — {datetime.now().strftime('%H:%M:%S')} "
              f"(Ctrl-C to stop)")
        if cmd_status(args) != 0:
            print("waiting for status file…")
        n += 1
        try:
            time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nstopped.")
            return 0
    return 0


# ----------------------------------------------------------------------
# report — static HTML with inline SVG (no JS, no external assets)
# ----------------------------------------------------------------------
def _svg_bars(events, procs):
    """One horizontal bar per process: green = up, red = restart tick."""
    if not events:
        return "<p>No events recorded yet.</p>"
    times = [e["ts"] for e in events]
    t0, t1 = min(times), max(times)
    try:
        start = datetime.fromisoformat(t0.replace("Z", "+00:00"))
        end = datetime.fromisoformat(t1.replace("Z", "+00:00"))
    except ValueError:
        return "<p>Could not parse event timestamps.</p>"
    span = max((end - start).total_seconds(), 1)
    width, height = 720, 28 * max(len(procs), 1) + 10
    parts = [f'<svg width="{width}" height="{height}" '
             'xmlns="http://www.w3.org/2000/svg" role="img">']
    for i, name in enumerate(procs):
        y = 10 + i * 28
        parts.append(f'<text x="0" y="{y + 12}" font-size="12" '
                     f'font-family="sans-serif">{html.escape(name)}</text>')
        parts.append(f'<rect x="120" y="{y}" width="580" height="16" '
                     'fill="#e5e7eb" rx="3"/>')
        for e in events:
            if e.get("process") != name:
                continue
            try:
                t = datetime.fromisoformat(
                    e["ts"].replace("Z", "+00:00"))
            except ValueError:
                continue
            x = 120 + 580 * (t - start).total_seconds() / span
            color = {"exited": "#dc2626", "restarted": "#16a34a",
                     "exhausted": "#7c3aed"}.get(e.get("event", ""),
                                                 "#6b7280")
            parts.append(f'<circle cx="{x:.1f}" cy="{y + 8}" r="5" '
                         f'fill="{color}"><title>{html.escape(e["ts"])} '
                         f'{html.escape(e.get("event", ""))}</title></circle>')
    parts.append("</svg>")
    parts.append("<p>● <span style='color:#dc2626'>exited</span> &nbsp; "
                 "● <span style='color:#16a34a'>restarted</span> &nbsp; "
                 "● <span style='color:#7c3aed'>exhausted</span></p>")
    return "".join(parts)


def cmd_report(args):
    try:
        status = load_status(args.status_file, args.target)
    except (OSError, RuntimeError, json.JSONDecodeError) as e:
        print(f"healthwatch: cannot read status: {e}", file=sys.stderr)
        return 1
    events = load_events(args.events_file, args.target)
    procs = [p["name"] for p in status.get("processes", [])]
    restarts = sum(p.get("restarts", 0)
                   for p in status.get("processes", []))
    rows = "".join(
        f"<tr><td>{html.escape(p['name'])}</td><td>{p['state']}</td>"
        f"<td>{p['pid']}</td><td>{p['restarts']}</td>"
        f"<td>{p['uptime_seconds']}s</td></tr>"
        for p in status.get("processes", []))
    timeline = "".join(
        f"<li><code>{html.escape(e.get('ts', ''))}</code> "
        f"<b>{html.escape(e.get('process', ''))}</b> — "
        f"{html.escape(e.get('event', ''))} "
        f"(pid {e.get('pid')}, exit {e.get('exit_code')})</li>"
        for e in events[-50:])
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    page = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>HealthWatch report</title>
<style>body{{font-family:sans-serif;max-width:900px;margin:2em auto;
padding:0 1em}}table{{border-collapse:collapse}}td,th{{border:1px solid
#ccc;padding:4px 10px}}th{{background:#f3f4f6}}</style></head>
<body><h1>QNX HealthWatch — report</h1>
<p>Generated {now} &middot; status snapshot
{html.escape(str(status.get('generated_at', '?')))} &middot;
daemon up {status.get('daemon_uptime_seconds', '?')}s &middot;
total restarts: {restarts}</p>
<h2>Processes</h2>
<table><tr><th>Name</th><th>State</th><th>PID</th><th>Restarts</th>
<th>Uptime</th></tr>{rows}</table>
<h2>Uptime / restart timeline</h2>
{_svg_bars(events, procs)}
<h2>Recent events (latest {min(len(events), 50)} of {len(events)})</h2>
<ul>{timeline or '<li>No events yet.</li>'}</ul>
<p><small>Generated by <code>healthwatch report</code> v{VERSION}.
Status source: {html.escape(args.status_file)}.</small></p>
</body></html>
"""
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"report written to {args.out} "
          f"({len(events)} events, {restarts} restarts)")
    return 0


# ----------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(prog="healthwatch",
                                description="QNX HealthWatch host tool")
    p.add_argument("--version", action="version", version=VERSION)
    sub = p.add_subparsers(dest="cmd", required=True)

    pv = sub.add_parser("validate", help="check a healthwatch.yaml file")
    pv.add_argument("config")

    ps = sub.add_parser("status", help="print current process table")
    ps.add_argument("--target", default=None,
                    help="ssh target, e.g. root@192.168.1.10")
    ps.add_argument("--status-file", default=DEFAULT_STATUS)
    ps.set_defaults(func=cmd_status)

    pw = sub.add_parser("watch", help="live table, refreshed each interval")
    pw.add_argument("--target", default=None)
    pw.add_argument("--status-file", default=DEFAULT_STATUS)
    pw.add_argument("--interval", type=float, default=1)
    pw.add_argument("--count", type=int, default=0,
                    help="0 = run until Ctrl-C")
    pw.set_defaults(func=cmd_watch)

    pr = sub.add_parser("report", help="write a static HTML report")
    pr.add_argument("--target", default=None)
    pr.add_argument("--status-file", default=DEFAULT_STATUS)
    pr.add_argument("--events-file", default=DEFAULT_EVENTS)
    pr.add_argument("--out", default="healthwatch-report.html")
    pr.set_defaults(func=cmd_report)

    args = p.parse_args(argv)
    if args.cmd == "validate":
        errors, procs = validate_config(args.config)
        if errors:
            print("INVALID:")
            for e in errors:
                print(f"  - {e}")
            return 1
        print(f"OK: {len(procs)} process(es): "
              + ", ".join(x["name"] for x in procs))
        return 0
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
