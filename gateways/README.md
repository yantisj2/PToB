# Timing gateways

Copyright (c) 2026 Jonathan Yantis. MIT.

Gateways connect PToB timing to host clocks and conventional network services.
Each target defines its clock owners, physical event association, source validity,
provisioning and evidence limits.

| Target | Implemented input and output | Guide |
| --- | --- | --- |
| [Raspberry Pi 5](pi5/README.md) | B504e M-HAT hardware PPS → chrony → NTP; Wi-Fi software PTP and guarded Ethernet PHC/PTP preparation | [Setup](../docs/pi5/setup.md) · [Monitor](pi5/dashboard/README.md) |

The [Linux USB/SOF shadow observer](pi5/usb/README.md) records diagnostic data
from a T096 RC alongside the M-HAT/GPIO path. USB clock discipline and
external-PTP → PToB operation remain
[planned experiments](../docs/pi5/bridge-work.md). See the
[research catalogue](../papers/README.md) for the scope of the measured results.
