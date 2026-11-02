# Ethernet clock ownership and recovery

Copyright (c) 2026 Jonathan Yantis. MIT.

The installed Pi 5 Model B receives GPIO PPS through Linux IRQ capture. Its
`gem-ptp-timer` PHC supports Ethernet packet timestamps, but reports zero external
timestamp channels, periodic outputs and configurable pins. The PPS capability
bit alone does not supply a physical PPS input. Read the actual PHC capabilities
with `remote.py inventory` before selecting another capture path.

## Clock owners

Chrony owns `CLOCK_REALTIME`: PToB PPS supplies phase, while network NTP supplies
coarse UTC seconds. The `ptob-phc2sys` supervisor owns system-to-PHC transfer.
`ptob-ptp4l` supervises a server-only Ethernet PTP process; it never follows a
network master or disciplines the system clock. Wi-Fi's separate PTPd service
has no clock-adjustment capability and uses an ARB timescale.

The PHC transfer runs even with Ethernet disconnected. Waiting for a PTP port
before starting this transfer would leave an old PHC ready to send stale time
when a cable is connected. The supervisor derives the operating TAI−UTC offset
from bracketed kernel `CLOCK_TAI`/`CLOCK_REALTIME` reads and passes it explicitly
to phc2sys. It refuses zero/unknown offsets and restarts transfer if the kernel
offset changes. Chrony's `leapsectz right/UTC` supplies the kernel leap table;
this operational mapping is not independent UTC verification. Pending leap or
unsynchronized chrony status withdraws Ethernet serving.

On Debian 13, `tzdata-legacy` supplies `/usr/share/zoneinfo/right/UTC`; the base
`tzdata` package alone does not. Provisioning and timing installation include
that dependency and refuse a missing leap timezone before configuring chrony.
A zero kernel TAI offset withdraws transfer and Ethernet serving. Do not work
around that fence by hard-coding an offset or inferring readiness from active
supervisor services. Verify the leap database and the observed kernel offset
after boot and after changes to the database.

The Ethernet supervisor starts its child only after three continuous seconds of:

- Fresh collector status, complete admitted receiver/PPS/chrony path and a
  current admitted source identity.
- Normal chrony leap status, a current transfer supervisor and matching PHC/TAI
  bindings.
- An adjacent PHC/system read with a bracket no wider than 100 µs and a measured
  midpoint residual within 50 µs after removing the TAI−UTC offset.

The 50 µs check is a startup/recovery health fence. It does not establish
sub-microsecond accuracy. Its read bracket, midpoint residual and denominator
must accompany any measurements. Adjacent user-space reads include scheduling
and device-access asymmetry; phc2sys servo residuals are another internal metric.
Neither is an independently measured Ethernet timestamp or physical PPS error.

A source incarnation, selected parent, TAI offset or transfer generation change
withdraws the child and restarts settling. Loss of admission or PPS withdraws
serving on the next collector/guard update. Collector status older than three
seconds or transfer status older than two seconds also withdraws. Both wrappers
poll every 250 ms. Each has a five-second systemd watchdog and cgroup child
cleanup, so a stalled or killed supervisor cannot leave its child serving
indefinitely. CPU affinity confines wrappers and children to CPUs 0–2, preserving
the separate GPIO PPS IRQ assignment to CPU 3. PHC logs summarize 16 updates.

Clock class remains 248, clock accuracy and variance remain unknown, source is
internal oscillator, and UTC-offset validity/time/frequency traceability are not
asserted. A running supervisor means its policy is operating; inspect
`/run/ptob-clocks/server.json` to distinguish a waiting supervisor from a running
PTP child. Physical link and remote packet delivery are separate observations.

## Updating an installed gateway

```sh
python3 gateways/pi5/tools/remote.py clock-install
python3 gateways/pi5/tools/remote.py timing-status
```

The exact-device SSH guard applies. This narrow installer audits existing clock
process ownership, preserves the old owned Ethernet configuration under
`/var/lib/ptob/pi5/clock-chain-before`, stops both owned Ethernet units and installs
only their script/configuration. It preserves chrony, Wi-Fi PTP, UART collection,
M-HAT firmware and network configuration. A failed installation leaves Ethernet
PTP stopped and retains its backup. `timing-install` also installs the clock
supervisors for a new gateway.

The two supervisor status files report transfer direction, operating TAI offset,
freshness and startup fence observations. Kernel offset changes are exercised
by offline tests; a real leap second is not simulated by stepping the operating
system clock. Do not start a second clock writer or bypass the wrapper with a
direct ptp4l command. The installer refuses external chronyd/phc2sys/ptp4l,
ts2phc, timemaster, ntpd and timesyncd owners; manual changes after installation
still need an ownership review.

## Prior work and the hardware boundary

Jeff Geerling's [CM4 PTP experiment](https://www.jeffgeerling.com/blog/2022/ptp-and-ieee-1588-hardware-timestamping-on-raspberry-pi-cm4/)
compares physical PPS outputs from two CM4 PHY clocks. The CM4 IO timing pin and
PHY are different from ordinary Pi 5 GPIO. His
[Time Pi configuration](https://github.com/geerlingguy/time-pi/blob/master/example.config.yml)
uses a PHC-first design with a NIC hardware PPS input and PHC-to-system transfer.
Its NIC-reset/edge/whole-second recovery experience motivates explicit clock
ownership, timescale handling and watchdogs here. This implementation was
written independently; no GPL Time Pi code was incorporated.

[Switchberry](https://github.com/Time-Appliances-Project/Switchberry) adds a
hardware transparent-clock switch, a DPLL and physical timing I/O. Its residence
time correction addresses network forwarding, while this gateway's GPIO IRQ
capture remains upstream of the PHC. Adding a transparent-clock
switch does not remove GPIO IRQ capture error. Neither its hardware features
nor a demonstration's PPS comparison qualifies this Pi gateway under load.

Direct PPS-to-PHC capture requires a different, verified PHC with an accessible
EXTTS input, supported driver and suitable voltage/edge handling, plus physical
wiring from the M-HAT. A compatible NIC/TimeHAT or a Compute Module timing carrier
is a candidate, not installed functionality. No GNSS addition is required to
carry PToB PPS. Independent PPS comparison and Ethernet slave measurements,
including source loss, link flaps, cable delay and idle/load populations, remain
necessary before a below-one-microsecond output claim.
