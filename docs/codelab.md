# Codelab: Put a Watchdog on QNX 8.0 in 30 Minutes

*With QNX HealthWatch — no embedded experience needed, QEMU only.*

> **What you will build:** a supervisor daemon (`healthd`) that
> watches a process, restarts it when it dies, and shows you the
> restarts from your laptop. **Time:** ~30 minutes once SDP 8.0 is
> installed (setup itself is a separate one-time job — see
> [SETUP.md](SETUP.md)).

## 0. What you need

- QNX SDP 8.0 installed (see SETUP.md), QEMU, Python 3 on the host.
- This repo, checked out in WSL2 (Ubuntu 24.04).

## 1. Build the two target binaries (5 min)

```sh
. ~/qnx800/qnxsdp-env.sh
make qnx
ls -l out/
# healthd  crashdemo
```

`healthd` is the supervisor. `crashdemo` is a demo app that dies
every ~10 seconds on purpose — your first "critical process".

## 2. Look at the config (2 min)

```sh
cat config/healthwatch.yaml
```

```yaml
check_interval: 1
status_file: /dev/shmem/health.json
events_file: /dev/shmem/health-events.jsonl
processes:
  - name: crashdemo
    command: /usr/bin/crashdemo
    restart: always
    max_restarts: 0
    restart_delay: 1
```

One entry per process: a `name`, the `command` to run, and the
restart policy. `max_restarts: 0` means "keep restarting forever".

## 3. Build the boot image (5 min)

```sh
cd qnx
cp ../out/healthd ../out/crashdemo .   # binaries must sit next to the buildfile
. ~/qnx800/qnxsdp-env.sh   # every new terminal needs this first
mkifs healthwatch.build healthwatch.ifs
# produces healthwatch.ifs
cd ..
```

The buildfile packs `healthd`, `crashdemo`, and the config into one
bootable image, and starts `healthd` at boot.

## 4. Boot it in QEMU (5 min)

```sh
./scripts/boot-qemu.sh qnx/healthwatch.ifs
```

In a second terminal, wait for SSH, then log in:

```sh
./scripts/wait-for-ssh.sh
ssh -p 2222 root@localhost
```

## 5. Watch it work (5 min)

On the target, check the status snapshot the daemon publishes:

```sh
cat /dev/shmem/health.json
slog2info -b healthd
```

Back on the host, run the live dashboard:

```sh
python3 host/healthwatch.py watch --target qnx
```

You will see `crashdemo` flip to `dead`, then back to `running`
with the restart counter going up — a restart takes under a second
after the 1-second check interval. Kill it yourself to prove it:

```sh
# on the target:
pidin | grep crashdemo
slay crashdemo
# watch the dashboard: it comes back by itself
```

## 6. Make the report (3 min)

```sh
python3 host/healthwatch.py report --target qnx --out report.html
```

Open `report.html`: a table of processes, an uptime/restart
timeline (red dot = died, green dot = restarted), and the recent
events. That HTML file is the artefact you can paste into a
post-mortem or a demo video.

## 7. Supervise your own process (5 min)

Edit `config/healthwatch.yaml` — add your app as a second entry:

```yaml
  - name: my-app
    command: /usr/bin/my-app --port 9000
    restart: always
    max_restarts: 5
    restart_delay: 2
```

Rebuild the image (step 3), reboot QEMU (step 4), and your app is
now supervised: if it crashes, it comes back, and every restart is
logged to `slog2` and to `/dev/shmem/health-events.jsonl`.

## What you just learned

- QNX has no built-in supervisor for user processes — `healthd`
  fills that gap with ~600 lines of C and no extra libraries.
- Supervision is three things: **detect** (poll the child),
  **restart** (with backoff, so a crashing process cannot spin),
  and **record** (slog2 + JSON status + events log).
- The status file (`/dev/shmem/health.json`) is the contract between the
  target and the host tool — that is what makes the dashboard and
  the report possible without any QNX-specific code on the host.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `healthd` not in the image | binaries not copied next to the buildfile | `cp ../out/healthd ../out/crashdemo .` in `qnx/` and rebuild |
| mkifs: "Host file 'elf.boot' not available" | buildfile used `[virtual=...,elf]` | use `[virtual=x86_64,multiboot]` (already fixed in this repo) |
| SSH never answers | Guest networking / hostfwd | Check `ifconfig wm0` in the QEMU console; confirm `-netdev …hostfwd=tcp::2222-:22` |
| Restarts = 0, process stays dead | `restart: never` or `max_restarts` reached | Check the config; look at `slog2info -b healthd` for "exhausted" |
| Status file missing | `healthd` failed to start | Run it in the foreground on target: `healthd -c /etc/healthwatch.yaml -f` and read the error |

## Next steps

- **Week 2:** add the `/dev/healthwatch` resource manager (see
  [ROADMAP.md](ROADMAP.md)) so status is readable as a device file.
- **Ideas:** alert on the third restart in a minute; watch a
  process you did *not* start (PID file mode); add a `healthd`
  config reload on SIGHUP.
