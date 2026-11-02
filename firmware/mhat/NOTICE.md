# M-HAT receiver notices

Copyright (c) 2026 Jonathan Yantis. Original adapters, board description, tools
and tests use the repository [MIT license](../../LICENSE).

Selected current T096 sources retain their per-file Apache-2.0 and MIT notices;
see the [T096 notice](../t096/NOTICE.md) and
[Apache-2.0 text](../../LICENSES/Apache-2.0.txt). No private workspace source or
private Git history is imported by this target.

Zephyr, Nordic HAL, CMSIS, TinyCrypt and the SDK are separately acquired through
the public T096 dependency lock and retain their terms. Particle source was
consulted for layout, validation and the retained DFU request ABI; no Particle
source or binary is included here. Recovery binaries remain ignored locally.

Local Particle tools retain their notices. A matching CRC is not hardware or
accuracy acceptance. Binary distribution needs complete linked dependency
notices and a separately reviewed USB VID/PID assignment.
