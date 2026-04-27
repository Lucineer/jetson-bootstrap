# jetson-bootstrap

> "Git history is your memory. Clone this repo, you are a few commits behind, not starting over."

## What This Is

A git-agent that captures everything needed to replicate a Jetson-based agent on another Jetson. Clone this repo on another Jetson and be operational within minutes.

## This Jetson Profile

- Model: Jetson Orin Nano (Engineering Reference Developer Kit)
- CPU: 6x ARM Cortex-A78AE @ 1516 MHz
- GPU: 1024 CUDA cores (sm_87)
- RAM: 7619 MB unified (8GB physical)
- CUDA: 12.6 at /usr/local/cuda/bin/nvcc
- Fingerprint: 0xdccfacd6efe83b4d

## Build

```bash
gcc -std=c99 -Wall -Wextra -O2 -o jetson-bootstrap jetson-bootstrap.c -lm
./jetson-bootstrap
```

## Tests

12/12 passing.

## Zero Dependencies

C99. Static allocation. -lm only.

---

Part of the [Cocapn Fleet](https://github.com/Lucineer).

---

## Fleet Context

Part of the Lucineer/Cocapn fleet. See [fleet-onboarding](https://github.com/Lucineer/fleet-onboarding) for boarding protocol.

- **Vessel:** JetsonClaw1 (Jetson Orin Nano 8GB)
- **Domain:** Low-level systems, CUDA, edge computing
- **Comms:** Bottles via Forgemaster/Oracle1, Matrix #fleet-ops
