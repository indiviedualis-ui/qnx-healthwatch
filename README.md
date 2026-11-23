# QNX HealthWatch

A process supervisor for QNX 8.0: a small daemon (`healthd`) that
watches your critical processes, restarts them when they die, logs
every event to `slog2`, and publishes a live status snapshot — plus
a host tool (`healthwatch`) that turns that snapshot into a live
dashboard and an HTML report.

Built for the **QNX Global Hackathon 2026** (category: **Developer
Tooling & Host Tools**). Total prizes $50,000; deadline
**15 December 2026, 14:00 ET (16 Dec 2026, 03:00 WITA)**.
Official site: https://qnx.devpost.com/

> **Status: v1, proven on real QNX 8.0 in QEMU (2026-11-23).** The
> supervisor loop, YAML parser, backoff, status JSON and host tool
> pass `make test` on Linux (see [docs/TESTING.md](docs/TESTING.md)),
> and the full QNX run is proven: SDP 8.0, boot image built with
> `mkifs`, kernel boots in QEMU, `healthd` supervises `crashdemo`
> (segfault, exit 139), restarts it with exponential backoff
> (1s/2s/4s/8s/16s/30s cap), and writes a valid
> `/dev/shmem/health.json` status snapshot — verified live in the
> guest. See [docs/TESTING.md](docs/TESTING.md) for the proof and
> the known limits (guest networking/SSH still in progress;
> `random` needs the bundled `/etc/qcrypto.conf`).

## The problem

In a real-time system — a car, a medical device, a robot — one
critical process dying silently can be fatal. QNX gives you the
pieces to notice (`pidin`, `slog2info`) but no easy supervisor:
no restart, no dashboard, no audit trail. Developers end up
hand-rolling shell scripts around `pidin`, and those scripts are
fragile.

HealthWatch is the missing piece: **detect, restart, record.**

- **Detect** — `healthd` polls every supervised process once a
  second.
- **Restart** — a dead process comes back automatically, with
  exponential backoff (so a crashing process cannot spin) and a
  restart limit (`max_restarts`).
- **Record** — every start, exit, restart and give-up is logged to
  `slog2` (readable with `slog2info -b healthd`), to a JSONL events
  file, and to a live status snapshot (`/dev/shmem/health.json`).

Unlike telemetry collectors (rtcollector) or read-only diagnostic
servers (QNX MCP Server), HealthWatch **acts**: it brings the
process back.

## Quick start (30-minute codelab)

See [docs/codelab.md](docs/codelab.md) — build, boot in QEMU, watch
`crashdemo` die and come back, generate the HTML report.

## Layout

```
config/healthwatch.yaml   example config (crashdemo, restart always)
src/healthd.c             supervisor daemon (C11, qcc + -lslog2 on QNX)
src/crashdemo.c           demo app that crashes every ~10 s on purpose
host/healthwatch.py       host tool, stdlib Python only
                          validate | status | watch | report
qnx/healthwatch.build     mkifs buildfile for the QEMU boot image
scripts/boot-qemu.sh      boot the IFS in QEMU (SSH on localhost:2222)
scripts/wait-for-ssh.sh   wait until the guest answers SSH
docs/SETUP.md             SDP 8.0 install (incl. the myQNX blocker)
docs/codelab.md           30-minute codelab (also a bonus-prize entry)
docs/TESTING.md           what was actually tested, and what was not
docs/ROADMAP.md           v2: /dev/healthwatch resource manager, etc.
tests/test_healthd.py     Linux integration test (make test)
examples/                 sample status JSON + events log + report
```

## Configuration

```yaml
check_interval: 1
status_file: /dev/shmem/health.json
events_file: /dev/shmem/health-events.jsonl
processes:
  - name: crashdemo
    command: /usr/bin/crashdemo
    restart: always
    max_restarts: 0      # 0 = unlimited
    restart_delay: 1     # initial backoff, seconds (doubles, cap 30s)
```

Validate a config without running the daemon:

```sh
python3 host/healthwatch.py validate config/healthwatch.yaml
```

## Build

```sh
# QNX target (SDP 8.0, after sourcing qnxsdp-env.sh):
make qnx            # qcc -Vgcc_ntox86_64, links -lslog2
make qnx-aarch64    # Raspberry Pi 5 class targets

# Linux logic test:
make linux
make test           # full integration test, ~25 s
```

## Run

On the target:

```sh
healthd -c /etc/healthwatch.yaml        # daemonize (default)
healthd -c /etc/healthwatch.yaml -f     # foreground, for debugging
```

(The QEMU boot image runs `healthd -f` backgrounded from the
`.script`: the daemonized form supervises fine but, on this QNX
8.0 image, does not write the status file — see
[docs/TESTING.md](docs/TESTING.md).)

On the host (SSH to the target, port 2222 via QEMU hostfwd):

```sh
python3 host/healthwatch.py status --target qnx
python3 host/healthwatch.py watch  --target qnx
python3 host/healthwatch.py report --target qnx --out report.html
```

(`--target` is an ssh target; add a `~/.ssh/config` entry for
`qnx` with `Port 2222` — see docs/SETUP.md.)

## Status snapshot

`healthd` writes `/dev/shmem/health.json` directly (plain write,
once per check interval). Note: it does **not** use the tmp-file +
rename pattern — `rename()` silently fails inside `/dev/shmem` on
QNX 8.0, so a direct write is required (verified 2026-11-23).
The boot image's own directories are read-only at runtime, which
is why the status lives in `/dev/shmem` (a RAM filesystem).

```json
{
  "version": "1.0.0",
  "generated_at": "2026-11-23T10:38:47Z",
  "daemon_uptime_seconds": 68,
  "processes": [
    {
      "name": "crashdemo",
      "command": "/usr/bin/crashdemo",
      "state": "restarting",
      "pid": 0,
      "restarts": 5,
      "max_restarts": 0,
      "uptime_seconds": 52,
      "last_exit_code": 139
    }
  ]
}
```

(The above is a real snapshot read from `/dev/shmem/health.json`
inside the QEMU guest on 2026-11-23 — `crashdemo` in backoff
after its 5th segfault, about to be restarted.)

States: `running`, `dead`, `restarting` (in backoff, about to be
respawned), `exhausted` (hit `max_restarts`). `last_exit_code` is
the exit status as seen by the shell that launched the process
(e.g. `139` = died of SIGSEGV, `128 + signal`).

## License

MIT — see [LICENSE](LICENSE).
