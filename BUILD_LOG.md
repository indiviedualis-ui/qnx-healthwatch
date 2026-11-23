# Build log — HealthWatch v1

## 23 Nov 2026 (WITA), built in this VM (Linux x86_64, gcc 13, Python 3.12)

### Setup research (verified from public sources)
- QNX Everywhere = free SDP 8.0 for non-commercial use, but it needs a
  **myQNX account + licence accept/deploy** by Nendra, then the QNX
  Software Center download. No account exists here, so SDP 8.0 could
  **not** be installed in this VM. See `docs/SETUP.md`.
- slog2 API verified against QNX docs: `slog2_register()` with a
  `slog2_buffer_set_config_t`, then `slog2f(handle, 0, SLOG2_INFO, …)`,
  link `-lslog2`. That is the exact API used in `src/healthd.c`
  (`#ifdef __QNX__` only).
- WSL2 path verified against the published tutorial: WSL2 Ubuntu
  24.04 → SDP 8.0 (~6.4 GB) → QEMU x86_64 → SSH localhost:2222.

### What was built
- `src/healthd.c` (538 lines): YAML-subset parser (no libyaml),
  fork/exec supervisor, 1 s poll, exponential backoff (1→2→4→8…,
  cap 30 s, reset after a 60 s healthy run), `max_restarts`,
  slog2 on QNX / stderr on Linux, atomic JSON status file,
  JSONL events log, graceful shutdown (SIGTERM → SIGKILL).
- `src/crashdemo.c`: dies of SIGSEGV after ~5–15 s (or
  `CRASH_AFTER=n`, `CRASH_MODE=exit`).
- `host/healthwatch.py` (321 lines, stdlib only): `validate`,
  `status`, `watch`, `report` (static HTML + inline SVG timeline).
- `config/healthwatch.yaml`, `Makefile` (`make qnx` /
  `make qnx-aarch64` / `make linux` / `make test`),
  `qnx/healthwatch.build` (mkifs), `scripts/boot-qemu.sh`,
  `scripts/wait-for-ssh.sh`, `docs/{SETUP,codelab,TESTING,ROADMAP}.md`,
  `tests/test_healthd.py`, MIT LICENSE, README.

### Test results — `make test` — ALL PASSED (23 Nov 2026)
- Config validates.
- 20 s live run: crashdemo (dies every ~2 s) restarted ≥3 times,
  never gave up (state `running`/`restarting`); stable sleeper kept
  0 restarts; events log has `exited` + `restarted` entries.
- Restart limit: instant-`exit 1` process with `max_restarts: 2`
  ends with exactly 2 restarts and state `exhausted`.
- Host tool `status` and `report` work; real report saved as
  `examples/report.html`, real events as
  `examples/health-events.jsonl`.
- Build is warning-free with `gcc -Wall -Wextra`.

### Bugs found and fixed during the build
1. `daemon()` implicit declaration — fixed with `_DEFAULT_SOURCE`.
2. Validator rejected the `processes:` header — fixed (skip the line).
3. Status went stale during backoff sleep — fixed by publishing the
   status before the backoff sleep, and added the `restarting` state.
4. Test read the status after shutdown (children already killed) —
   fixed by reading it while the daemon still runs.

### NOT done / NOT tested (honest list)
- `make qnx` — no `qcc`, no SDP 8.0 here.
- mkifs image build, QEMU boot, SSH, `slog2info -b healthd` — need
  SDP install on Nendra's Windows/WSL2 (docs/SETUP.md steps 1–8).
- `/dev/healthwatch` resource manager — v2, see docs/ROADMAP.md.
- No Devpost registration, no public GitHub repo, no submission.
