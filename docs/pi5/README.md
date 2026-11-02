# Raspberry Pi timing gateway guides

Copyright (c) 2026 Jonathan Yantis. MIT.

Start with the [Pi target](../../gateways/pi5/README.md).

| Guide | Scope |
| --- | --- |
| [Setup](setup.md) | OS/SSH identity, HAT bus, provisioning, services and verification |
| [Timing and diagnostics](timing.md) | PPS/chrony, source admission, history, tails and wire checks |
| [Clock ownership and recovery](clock-chain.md) | PHC/PTP supervisors, TAI, withdrawal and native hardware limits |
| [M-HAT access](mhat-access.md) | Exact USB identification, bus isolation and power prerequisites |
| [Next bridge experiments](bridge-work.md) | Proposed T096 RC USB/SOF test and future Ethernet/reverse PTP work |
| [Monitoring website](../../gateways/pi5/dashboard/README.md) | Generic screenshots, charts, exports and offline preview |

The [paper and selected measurements](../../papers/pi5-mhat-gateway/README.md)
are a separate dated research population. Instructions describe current source
capabilities; low internal RMS or protocol liveness does not qualify output
accuracy. Hardware changes and new test populations need their own exact-device
scope and retained evidence.
