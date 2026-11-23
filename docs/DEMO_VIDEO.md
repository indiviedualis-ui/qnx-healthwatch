# Demo video — shot list (2–3 minutes)
<!-- Record on the Windows laptop with Xbox Game Bar (Win+G) while
     the QEMU window is focused, or OBS. Upload to YouTube
     (unlisted is fine) and paste the link into Devpost. -->

## Before recording
- Boot QEMU once with the current image so the boot is fast
  and the demo is repeatable: `./scripts/boot-qemu.sh`
- Have a second terminal ready on the host for the `make test`
  clip (optional).

## Shots (in order, ~2–3 min total)
1. **Title card (5 s)** — show the README title, or a plain
   slide: "HealthWatch — a process supervisor for QNX 8.0".
2. **Boot (20 s)** — run `./scripts/boot-qemu.sh`. Show
   "Loading IFS...done", the kernel starting, and the `#`
   prompt appearing.
3. **The supervisor working (60 s)** — let the console scroll.
   Point out (voiceover or captions):
   - `crashdemo` heartbeats (1/14, 2/14, …),
   - `dying now` + `Memory fault`,
   - `healthd WARN crashdemo exited code 139`,
   - `healthd INFO restarting crashdemo in 1s/2s/4s…`
     (exponential backoff, capped at 30s).
4. **The status file (30 s)** — at the `#` prompt, type:
   `test -f /dev/shmem/health.json && echo ADA || echo TIDAK`
   (shows ADA), then
   `while read l; do echo "$l"; done < /dev/shmem/health.json`
   and show the JSON: version, daemon_uptime_seconds,
   crashdemo `state: restarting`, `restarts: 5`,
   `last_exit_code: 139`.
5. **Processes running (15 s)** — type `pidin` and show
   `healthd` + `crashdemo` in the list.
6. **Host tool on Linux (20 s, optional)** — on the host,
   `make test` and show `ALL TESTS PASSED`.
7. **Closing card (5 s)** — "HealthWatch — Detect, restart,
   record. QNX Global Hackathon 2026." + repo link.

## Tips
- Keep the QEMU window maximised; the console font is small,
  so zoom the recording to the terminal if needed.
- Do not press Ctrl-C during the demo (it kills QEMU). To end
  the recording, stop the recorder first, then Ctrl-C.
- If the healthd logs scroll too fast to read, that is fine —
  the point is that it keeps working; pause the video on the
  `health.json` output for shot 4.
