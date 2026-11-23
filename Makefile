# QNX HealthWatch — build
#
# On QNX (SDP 8.0, after sourcing qnxsdp-env.sh):
#   make qnx          # qcc, x86_64 target, links -lslog2
#   make qnx-aarch64  # qcc, aarch64 target (e.g. Raspberry Pi 5)
#
# On Linux (logic test only — same code, slog2 replaced by stderr):
#   make linux        # gcc, -DHEALTHWATCH_LINUX_TEST
#   make test         # build + run the Linux supervisor test
#
# Install on target: copy healthd, crashdemo and healthwatch.yaml
# to the IFS (see qnx/healthwatch.build) or to /usr/bin on a
# running target, then:  healthd -c /etc/healthwatch.yaml

CC_QNX   ?= qcc
QNX_CPU  ?= gcc_ntox86_64
CFLAGS   ?= -O2 -std=c11 -Wall -Wextra
OUTDIR   ?= out

.PHONY: all qnx qnx-aarch64 linux test clean

all: linux

qnx: $(OUTDIR)/healthd $(OUTDIR)/crashdemo

qnx-aarch64: QNX_CPU=gcc_ntoaarch64le
qnx-aarch64: qnx

$(OUTDIR)/healthd: src/healthd.c
	mkdir -p $(OUTDIR)
	$(CC_QNX) -V$(QNX_CPU) $(CFLAGS) -o $@ src/healthd.c -lslog2

$(OUTDIR)/crashdemo: src/crashdemo.c
	mkdir -p $(OUTDIR)
	$(CC_QNX) -V$(QNX_CPU) $(CFLAGS) -o $@ src/crashdemo.c

linux: out-linux/healthd out-linux/crashdemo

out-linux/healthd: src/healthd.c
	mkdir -p out-linux
	gcc $(CFLAGS) -DHEALTHWATCH_LINUX_TEST -o $@ src/healthd.c

out-linux/crashdemo: src/crashdemo.c
	mkdir -p out-linux
	gcc $(CFLAGS) -o $@ src/crashdemo.c

test: linux
	python3 tests/test_healthd.py

clean:
	rm -rf out out-linux __pycache__ host/__pycache__ tests/__pycache__
	rm -f /tmp/health.json /tmp/health-events.jsonl
