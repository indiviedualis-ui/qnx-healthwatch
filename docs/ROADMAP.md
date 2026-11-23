# Roadmap — after v1

## v1 (this repo, done)
- `healthd`: YAML config, spawn + poll, exponential backoff (cap 30 s),
  `max_restarts`, slog2 logging on QNX, JSON status file
  (`/dev/shmem/health.json`, atomic write), JSONL events log.
- `healthwatch` host tool (stdlib Python): `validate`, `status`,
  `watch`, `report` (HTML + SVG timeline).
- `crashdemo` demo app, example config, mkifs buildfile, QEMU scripts,
  codelab, README + setup docs.
- Tested on Linux only (`make test`) — see TESTING.md.

## v2 (week 2, if v1 is stable on QEMU)
- **`/dev/healthwatch` resource manager** — the idiomatic QNX way to
  expose status: `resmgr_attach()` + `dispatch_create()` +
  `iofunc_*` handlers; `cat /dev/healthwatch` returns the same JSON
  the status file holds. Keep the status file as a fallback.
- **PID-file watch mode** — supervise a process healthd did not
  start (read its PID from a file, `kill(pid, 0)` to probe).
- **SIGHUP config reload** — re-read the YAML without restarting.
- **Alert threshold** — log a distinct `alert` event (and mark the
  process in the report) after 3 restarts within 60 s.

## v3 (only if time remains before 15 Dec)
- Health check hooks: run `command --health-check` and treat a
  non-zero exit as "unhealthy" even if the process is alive.
- `healthwatch` SSH multiplexing + `--target` from a config file.
- Demo video script (shot list for the <3 min YouTube demo).

## Explicitly out of scope
- Telemetry exporters / Prometheus (rtcollector already does this).
- Read-only diagnosis servers (QNX MCP Server already does this).
- Anything that touches production systems or other people's data.
