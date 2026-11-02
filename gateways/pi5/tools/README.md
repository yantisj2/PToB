# Pi gateway scripts

Copyright (c) 2026 Jonathan Yantis. MIT.

Run commands from the repository root using the [setup guide](../../../docs/pi5/setup.md).
`remote.py` is the guarded workstation entry point; remote implementation modules
are not standalone setup commands. Every remote payload checks the enrolled Pi's
factory serial, model/architecture and boot-image marker over pinned-key SSH.
Keys, connection profiles, enrollment, retained builds and reports remain private.

| Script / action | Purpose | Effect |
| --- | --- | --- |
| `remote.py inventory` / `probe.py` | OS, controllers, clocks, PHC and interfaces | Read-only target inventory |
| `remote.py provision` / `setup_target.py` | Missing signed OS development packages | Installs packages; use before timing services |
| `remote.py smoke` / `clock_smoke.c` | Native BlueZ/systemd build and clock capabilities | Compiles local target output; no timing packets |
| `remote.py timing-install` / `timing_setup.py` | PPS overlay, chrony, NTP and both PTP paths | Changes clock/service configuration and enables serving |
| `remote.py timing-status` | PPS, chrony, actual PHC/PTP guards and link status | Read-only target status |
| `remote.py clock-install` / `clock_chain.py` | Existing gateway's guarded Ethernet clock chain | Updates/restarts owned Ethernet supervisors only |
| `remote.py dashboard-install` / `dashboard_setup.py` | Exact installed HAT source binding, collector and UI | Restarts collector and web service |
| `remote.py jitter-install` / `pps_tune.py` | Bound GPIO16 IRQ and CPU governor policy | Changes host IRQ/CPU policy, persists across boot |
| `remote.py tailscale-install` / `tailscale_setup.py` | Optional operator-owned private access | Installs/authenticates VPN and private Serve |
| `remote.py tailscale-status` | Optional private access status | Read-only; report can contain private addresses |
| `mhat_usb.cjs` | Exact B504e USB identification | Bounded read-only inventory, no reset or flash |
| `ntp_probe.py` | Four NTP exchanges to configured Pi | Sends network requests; RTT is not accuracy |
| `ptp_observe.py` | Bounded Wi-Fi domain-24 receiver check | Binds UDP319/320 and sends client requests; never adjusts clocks |
| `pps_capture.py --capture` | Cached sysfs PPS at 20 Hz plus source snapshots | Bounded target observation; no clock/configuration changes |
| `pps_capture.py --analyse` | Recompute full populations and tails | Offline; no hardware or private credentials needed beyond the input capture |

Use `--connection /absolute/private/pi5.json` for another explicitly enrolled Pi.
Report paths must be new files under ignored `artifacts/`; choose unique names.
Network probes use the configured address and do not silently substitute another
interface. Check local UDP port availability/privileges before a PTP probe.

The generic frontend preview is `../dashboard/preview.py`; it creates synthetic
observations locally and never opens a connection profile or hardware. The
paper's frozen observer snapshots under `evidence/pi5/` describe a separate
experimental population, not an alternative production installer.

M-HAT native builders, exact-device USB DFU/install and recovery tools live in
[`firmware/mhat/tools`](../../../firmware/mhat/README.md). Build outputs use the
shared [retention lifecycle](../../../docs/firmware-storage.md). The
[future bridge plan](../../../docs/pi5/bridge-work.md) records the proposed Linux
T096 USB/SOF input and reverse PTP direction; neither is an implemented script.
