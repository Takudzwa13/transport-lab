# Self-contained PA2 Transport Forensics Lab (C++)

This package is **fully self-contained**: it includes both

1. an **event-driven network emulator** (`emulator.cpp`), and  
2. a **black-box transport protocol** (`mystery_transport.cpp`).

Students should treat `mystery_transport.cpp` as **DO NOT MODIFY** (black box).
They are allowed to add **instrumentation/logging** in the emulator layer, and they may change
runtime parameters (window size, timeout, loss/corruption, delay, offered load, seeds, etc.)
to perform the required forensic analysis and collapse detection experiments.

---

## Files

- `simulator.hpp`  
  Shared types (`msg`, `pkt`) and the PA2-style hook declarations.

- `mystery_transport.cpp`  
  Black-box transport implementation. Intentionally contains suboptimal logic:
  * cumulative ACKs only
  * single timer
  * timeout triggers retransmission of *all* outstanding packets (amplifies collapse)

- `emulator.cpp`  
  Minimal event-driven simulator and driver:
  * event queue with deterministic ordering
  * loss + corruption + delay channel
  * timer events
  * generates application messages from A -> B
  * writes CSV logs and a summary

- `Makefile`  
  Builds `./sim`

---

## Build

```bash
make
```

This produces:

```bash
./sim
```

---

## Run

Run with `--help` to see all options:

```bash
./sim --help
```

Example run:

```bash
./sim --msgs 2000 --interval 5.0 --loss 0.2 --corrupt 0.1 --delay 10 --jitter 5 \
      --win 8 --timeout 20 --seed 1234 --out out_run1
```

Or via Make:

```bash
make run ARGS="--msgs 2000 --interval 5 --loss 0.2 --corrupt 0.1 --win 8 --timeout 20 --seed 1234 --out out_run1"
```

---

## Output

Each run writes to the directory specified by `--out` (default: `out/`):

- `events.csv`  
  High-level events: timers, layer5 deliveries, drops.

- `packets.csv`  
  Packet-level send/receive logs: timestamps, entity, seq/ack, etc.

- `summary.txt`  
  Basic counters and approximate goodput/send rates.

These logs are intended for student analysis and plotting.

---

