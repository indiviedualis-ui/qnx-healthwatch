# Setup — QNX SDP 8.0 + QEMU (Windows / WSL2 path)

Verified facts (checked 23 Nov 2026, sources in README):
- QNX Everywhere gives **free** SDP 8.0 access for non-commercial use.
  You need a **myQNX account**; the licence is free, but it is tied to
  a person — an agent cannot create it for you.
- SDP 8.0 needs a 64-bit x86 host (Windows 10/11 or Ubuntu 20.04/22.04
  LTS, RHEL 9). It does **not** support macOS or ARM hosts.
- Installed size ≈ **6.4 GB** (≈7 GB free disk in WSL); QEMU guest uses
  1 GB RAM; 8 GB host RAM is enough.
- The WSL2 tutorial that this repo follows was replayed on
  2026-11-09 for steps 4–7 (build image, boot QEMU, SSH, trace).

## Blocker, stated plainly

**SDP 8.0 is NOT installed in this workspace and could NOT be
installed here.** Downloading it requires:
1. a myQNX account in Nendra's name, and
2. accepting + deploying the free QNX Everywhere licence in that
   account, then
3. downloading the QNX Software Center installer from myQNX and
   installing SDP 8.0 through it.

None of those steps can be done by an agent without Nendra's account.
So everything QNX-specific in this repo — `make qnx`, the mkifs
buildfile, the QEMU boot scripts — is **written from the published
docs and tutorial, not yet run against real QNX**. The Linux test
(`make test`) is the part that *has* been run; see TESTING.md.

## Steps for Nendra (Windows laptop)

1. **myQNX + licence** (browser, ~10 min)
   - Go to https://qnx.software/en/developers/get-started/qnx-everywhere
     (or https://www.qnx.com/getqnx).
   - Create / log in to your myQNX account, complete the QNX
     Everywhere licence form, then **accept and deploy** the licence
     in myQNX License Manager. Allow up to 1 hour for it to appear
     in your profile.
2. **WSL2 Ubuntu 24.04** (PowerShell as Administrator)
   - `wsl --install -d Ubuntu-24.04`
   - Reboot if asked, create a Linux username/password.
3. **QNX Software Center (Linux host build)**
   - In your Windows browser, download the QNX Software Center
     installer for **Linux Hosts** from your myQNX account.
   - Copy it into WSL and run it as your normal user (no `sudo`):
     it lands in `~/qnx/qnxsoftwarecenter`.
   - Start it (`~/qnx/qnxsoftwarecenter/qnxsoftwarecenter`, opens
     on the Windows desktop via WSLg), sign in, install
     **QNX SDP 8.0** (package `com.qnx.qnx800`) into `~/qnx800`.
4. **QEMU + tools in WSL**
   - `sudo apt update && sudo apt install -y qemu-system-x86 openssh-client python3`
5. **Build HealthWatch for QNX**
   - `cd` into this repo in WSL, then:
     - `. ~/qnx800/qnxsdp-env.sh` (sets up `qcc` etc.)
     - `make qnx` → `out/healthd`, `out/crashdemo`
6. **Build the boot image**
   - `cd qnx`, copy the binaries next to the buildfile
     (`cp ../out/healthd ../out/crashdemo .`), then
     `. ~/qnx800/qnxsdp-env.sh` and
     `mkifs healthwatch.build healthwatch.ifs`.
     Do not use `mkqnximage --build=...` — SDP 8.0 rejects it, and
     the `elf` image format needs a BSP file the base SDP lacks;
     the buildfile uses the `multiboot` format QEMU loads.
7. **Boot + SSH**
   - `./scripts/boot-qemu.sh qnx/healthwatch.ifs`
   - In another WSL terminal: `./scripts/wait-for-ssh.sh`
   - SSH: `ssh -p 2222 root@localhost` (no password on the QSTI
     image by default; set one if your image requires it).
   - On target: `slog2info -b healthd` shows the daemon's log;
     `cat /dev/shmem/health.json` shows the status snapshot.
8. **Host tool from WSL/Windows**
   - `python3 host/healthwatch.py status --target root@localhost`
     (for port 2222, add a `~/.ssh/config` entry:
     `Host qnx\n  HostName localhost\n  Port 2222\n  User root`
     then use `--target qnx`).
   - `python3 host/healthwatch.py watch --target qnx`
   - `python3 host/healthwatch.py report --target qnx --out report.html`

## If the first boot fails

- Check the QEMU serial output for the `startup-x86` / `procnto`
  lines; a missing driver usually shows as a hang after `waitfor`.
- Verify the IFS actually contains healthd: `mkifs` prints the
  files it packs; if `healthd` is missing, copy the binaries next
  to the buildfile (`cp ../out/healthd ../out/crashdemo .` in `qnx/`).
- If SSH never answers, confirm the guest got an address
  (`ifconfig wm0` in the QEMU console) and that hostfwd is
  `tcp::2222-:22`.
