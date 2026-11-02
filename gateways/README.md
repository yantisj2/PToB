# Timing gateways

Copyright (c) 2026 Jonathan Yantis. MIT.

Gateways connect PToB timing to host clocks and conventional network services.
Each target defines its clock owners, physical event association, source validity,
provisioning and evidence limits.

| Target | Implemented input and output | Guide |
| --- | --- | --- |
| [Raspberry Pi 5](pi5/README.md) | B504e M-HAT hardware PPS → chrony → NTP; Wi-Fi software PTP and guarded Ethernet PHC/PTP preparation | [Setup](../docs/pi5/setup.md) · [Monitor](pi5/dashboard/README.md) |

Linux USB/SOF input from a T096 RC and external-PTP → PToB operation remain
[planned experiments](../docs/pi5/bridge-work.md). Source-fit consistency,
protocol health, finite availability and independent output accuracy are
separate claims. See the [research catalogue](../papers/README.md).
