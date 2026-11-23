# Devpost submission draft — QNX HealthWatch
<!-- Copy-paste ready for https://qnx.devpost.com/ (project by indiviedualis).
     Deadline: 15 Dec 2026, 14:00 ET (16 Dec 2026, 03:00 WITA). -->

## Title
HealthWatch — a process supervisor for QNX 8.0

## Tagline
Detect, restart, record: the supervisor that brings your critical
QNX processes back when they die.

## Category
Developer Tooling & Host Tools

## Built with
C (C11), QNX SDP 8.0, mkifs, QEMU, Python (stdlib only), YAML

## Links
- Repository: (paste the repo URL after pushing — see below)
- Demo video: (paste the YouTube link after recording — see docs/DEMO_VIDEO.md)

## Inspiration
In a real-time system — a car, a medical device, a robot — one
critical process dying silently can be fatal. QNX gives you the
pieces to notice (`pidin`, `slog2info`) but no easy supervisor:
no restart, no dashboard, no audit trail. Developers end up
hand-rolling shell scripts around `pidin`, and those scripts are
fragile. HealthWatch is the missing piece.

## What it does
`healthd` is a small C daemon that watches the processes listed
in a YAML config, checks them once a second, and restarts them
when they die — with exponential backoff (so a crashing process
cannot spin) and a restart limit. Every start, exit, restart and
give-up is logged to `slog2`, to a JSONL events file, and to a
live status snapshot (`/dev/shmem/health.json`) that a host tool
reads and turns into a live dashboard and an HTML report.

- **Detect** — polls every supervised process once a second.
- **Restart** — a dead process comes back automatically, with
  exponential backoff (1s, 2s, 4s, 8s, 16s, capped at 30s) and
  a restart limit (`max_restarts`).
- **Record** — every event in `slog2`, a JSONL events file, and
  a live status JSON.

Unlike telemetry collectors or read-only diagnostic servers,
HealthWatch **acts**: it brings the process back.

## How we built it
- `healthd` in C11: fork/exec supervisor loop, a small YAML
  parser (no dependencies), `slog2` logging on QNX, plain
  stderr on Linux so the same code is testable with `make test`.
- Boot image with `mkifs` (SDP 8.0): `[virtual=x86_64,multiboot]`,
  every binary and shared library listed explicitly.
- Host tool in stdlib-only Python: `validate | status | watch |
  report` (the report is a self-contained HTML page).
- A 30-minute codelab (`docs/codelab.md`) that takes a developer
  from zero to a booting supervisor in QEMU.

## Challenges we ran into
Almost everything QNX 8.0 does differently from the 7.x docs:
`mkifs` does not auto-include `.script` binaries or their shared
libraries; it does not expand `$QNX_TARGET` in buildfiles; the
base SDP ships no `mkdir`/`cp`/`ls`; image directories are
read-only at runtime (so status lives in `/dev/shmem`); `rename()`
silently fails inside `/dev/shmem` (so the status file is written
directly); `random` needs a `/etc/qcrypto.conf` that is not
shipped in the SDP; and the e1000 driver loads but creates no
`em0` interface yet. Each one was found by reading the guest's
own output (`dumpifs`, `slog2info`, `pidin`) and is documented
in the buildfile header and `docs/TESTING.md`.

## Accomplishments that we're proud of
Proven on real QNX 8.0 in QEMU (23 Nov 2026): the kernel boots,
`healthd` supervises `crashdemo` through repeated segfaults
(exit 139), restarts it with backoff up to the 30 s cap, and
writes a valid `/dev/shmem/health.json` — read live from the
guest. The same code passes `make test` on Linux. The whole
thing, including the codelab, is reproducible from the repo.

## What we learned
- Never trust a doc version: QNX 8.0 renamed libraries, drivers
  and the whole networking stack (`io-sock`, not `io-pkt`).
- On an embedded target, the filesystem you boot from is not
  the filesystem you write to — read-only image dirs vs.
  writable `/dev/shmem` changes the design.
- A "silent" failure (rename in shmem, a missing config file)
  is found fastest by testing the exact operation in the guest,
  not by re-reading the docs.

## What's next for HealthWatch
- Guest networking (`em0`) + `sshd` so the host tool can read
  the status over SSH (in progress).
- `/dev/healthwatch` resource manager (v2): query and control
  the supervisor from any QNX process.
- Aarch64 target (Raspberry Pi 5 class) via `make qnx-aarch64`.

## Try it out
See the README and `docs/codelab.md` (30 minutes, SDP 8.0 +
QEMU). `make test` runs the Linux integration test with no QNX
install.

## Repo
Push the repo to GitHub/GitLab first (the project has no remote
yet), then paste the URL here. Suggested repo name:
`qnx-healthwatch`. The submission must include the source;
Devpost accepts a repo link, not a zip.
