#!/bin/sh
# boot-qemu.sh — boot the HealthWatch IFS in QEMU (x86_64).
#
# Prerequisites (see docs/SETUP.md): QNX SDP 8.0 installed in ~/qnx800,
# the IFS built with mkifs into qnx/healthwatch.ifs, and
# qemu-system-x86_64 installed on the host.
#
# Usage:
#   ./scripts/boot-qemu.sh [path/to/healthwatch.ifs]
#
# Networking: user-mode networking, hostfwd forwards host :2222 to the
# guest SSH port, so the host tool can run:
#   python3 host/healthwatch.py status --target root@localhost -p 2222
# (healthwatch.py uses plain `ssh`; pass -p via ~/.ssh/config or an
#  SSH alias — see docs/SETUP.md.)
#
# QEMU flags (verified 2026-11-23): this is QNX's own "From The Board
# Up" set (-cpu max, -smp 1, -display none, -serial stdio) plus the
# e1000 NIC for SSH. The earlier set (-cpu qemu64, -smp 2, -nographic,
# -serial mon:stdio) did NOT load the kernel — SeaBIOS fell through
# to network boot ("Booting from ROM.."). The exact culprit flag is
# still unknown; do not change these flags without re-testing.
# To stop QEMU, press Ctrl-C in this terminal.
set -eu

IFS_FILE="${1:-qnx/healthwatch.ifs}"
if [ ! -f "$IFS_FILE" ]; then
    echo "error: IFS not found: $IFS_FILE" >&2
    echo "build it first — see qnx/healthwatch.build and docs/SETUP.md" >&2
    exit 1
fi

exec qemu-system-x86_64 \
    -m 1024 \
    -cpu max \
    -smp 1 \
    -display none \
    -kernel "$IFS_FILE" \
    -serial stdio \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device e1000,netdev=net0
