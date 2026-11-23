#!/bin/sh
# wait-for-ssh.sh — wait until the QEMU guest answers SSH on localhost:2222.
# Usage: ./scripts/wait-for-ssh.sh [timeout_seconds]
# Exit 0 when SSH answers, 1 on timeout.
set -eu
TIMEOUT="${1:-120}"
i=0
while [ "$i" -lt "$TIMEOUT" ]; do
    if ssh -o BatchMode=yes -o ConnectTimeout=2 -o StrictHostKeyChecking=no \
           -p 2222 root@localhost true 2>/dev/null; then
        echo "SSH is up after ${i}s"
        exit 0
    fi
    i=$((i + 2))
    sleep 2
done
echo "timed out after ${TIMEOUT}s waiting for SSH on localhost:2222" >&2
exit 1
