# Testing — what was actually run, and what was not

## Run here (Linux VM, 23 Nov 2026) — PASSED

`make test` (see `tests/test_healthd.py`) builds the Linux binaries
with gcc and runs a real supervisor loop:

1. `healthwatch validate` accepts the test config.
2. `healthd -f` supervises `crashdemo` (dies every ~2 s) and a
   stable `sleep 300` for ~14 s.
3. Checks: crashdemo restarted ≥3 times and is running again;
   sleeper still running with 0 restarts; events log contains
   `exited` + `restarted`; host tool `status` and `report` work
   and the HTML contains the process names and the timeline.
4. Restart-limit check: a process that `exit 1`s instantly with
   `max_restarts: 2` ends up with exactly 2 restarts and state
   `exhausted` (backoff 1 s, then 2 s, then give up).

The Linux build is the *same* `healthd.c` as the QNX build — only
the logging backend differs (stderr on Linux, slog2 on QNX via
`#ifdef __QNX__`, linked with `-lslog2`). The YAML parser, the
fork/exec supervisor loop, the backoff, the status JSON and the
events log are the code that ran.

Re-run any time with:

```sh
make test
```

## Run on real QNX 8.0 in QEMU (laptop, 23 Nov 2026) — PROVEN

SDP 8.0 (QNX Everywhere licence, serial 911285-10488077) on WSL2
Ubuntu 24.04; `make qnx` builds `out/healthd` + `out/crashdemo`;
`mkifs healthwatch.build healthwatch.ifs` builds a ~16.7 MB boot
image; QEMU boots it with the "From The Board Up" flags
(`-cpu max -smp 1 -display none -kernel <ifs> -serial stdio`).

Verified live in the guest:

- `healthd` supervises `crashdemo`: segfault (`Memory fault`,
  exit code 139) detected, restarted with exponential backoff
  1s/2s/4s/8s/16s/30s (cap), restart #6+ observed.
- `/dev/shmem/health.json` is written and reads as valid JSON
  (real snapshot in `examples/health.json`: crashdemo
  `restarting`, restarts 5, last_exit_code 139).
- `random` runs (needs the bundled `/etc/qcrypto.conf`,
  `openssl-3 tags=*`, plus `qcrypto-openssl-3.so`); `io-sock`
  runs (`lo0` UP, TCP/IP stack alive).
- `pidin` confirms `healthd` + `crashdemo` + `sh` running.

Key QNX 8.0 findings (all in the buildfile header + `BUILD_LOG.md`):
`mkifs` does not auto-include `.script` binaries or their `.so`
files; `[search]` needs absolute paths (`$QNX_TARGET` is not
expanded) and must list `boot/sys` separately; the base SDP has no
`mkdir`/`cp`/`ls`; image directories are read-only at runtime, so
runtime-writable files live in `/dev/shmem`; `rename()` silently
fails inside `/dev/shmem`, so `healthd` writes the status file
directly; `[type=link]` with a bare target is unreliable — use
real files; `sshd` needs an absolute path.

## NOT yet proven — in progress / future work

- Guest networking (`em0`): `devs-em.so` loads but no `em0`
  interface is created (verified with `ifconfig`: only `lo0`,
  `enc0`, `pflog0`, `pfsync0`). SSH on localhost:2222 therefore
  does not connect yet, and the host tool's `status`/`watch`/
  `report` over SSH are untested on the guest (they are tested
  on Linux with a local target).
- `sshd` in the image: still reports "requires execution with an
  absolute path" even though the `.script` runs `/proc/boot/sshd`
  (unresolved); host keys are not yet provisioned.
- The `/dev/healthwatch` resource manager — v2, not written yet.

The README and the Devpost submission must say "QNX supervisor +
status file proven in QEMU; guest networking/SSH in progress"
— do not claim SSH or the host dashboard over SSH as tested.
