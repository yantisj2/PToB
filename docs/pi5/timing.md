# Pi gateway timing and diagnostics

Copyright (c) 2026 Jonathan Yantis. MIT.

The [Pi target](../../gateways/pi5/README.md) consumes PPS from the
[M-HAT receiver](../../firmware/mhat/README.md). Admission, PPS capture, clock
discipline and network transport are separate observations.

| Path | Connection / owner |
| --- | --- |
| PPS | B504e D2/RTS, P1.02 → Pi GPIO16, physical pin 36 |
| Diagnostics | B504e TX P0.06 → GPIO15; GPIO14 → B504e RX P0.08 |
| HAT switch | B504e A0/P0.03 high; firmware owns this |
| Header UART | `dtoverlay=uart0-pi5`, without `ctsrts`; normally `/dev/ttyAMA0` |
| System clock | `chrony.service`: PPS phase, NTP coarse UTC |
| Ethernet PHC | `ptob-phc2sys.service`: guarded system clock → PHC, independent of link |
| Ethernet PTP | `ptob-ptp4l.service`: guarded L2, E2E, hardware timestamps, domain 24 |
| Wi-Fi PTP | `ptob-ptp-wifi.service`: UDP, software timing, domain 24, ARB timescale |

`/dev/serial0` can refer to the Pi 5 debug UART. Check its device-tree binding.
UART flow control would take GPIO16 away from PPS. GPIO4 is HAT power control.
The custom `ptob-pps` overlay and udev rule supply `/dev/pps-ptob`; `/dev/pps0`
can instead belong to the Ethernet MAC. GPIO PPS capture uses IRQ timestamps.

## Setup

Enroll exact Pi factory/image identity and pinned SSH access before using
`gateways/pi5/tools/remote.py`. Its private profile defaults to
`~/.config/ptob/pi5.json`; keys and host pins stay in `~/.ssh/`.
Run baseline provisioning before enabling timing, then use the target README's
`timing-install`, `tailscale-install` and `dashboard-install` commands.

For diagnostics, add `dtoverlay=uart0-pi5` under `[all]` in the Pi's
`/boot/firmware/config.txt`; during an authorized live setup apply the same
overlay with `dtoverlay uart0-pi5`. Leave `ctsrts` off. The dashboard installer
verifies the RP1 header UART node.

The collector validates exact module, installed source and boot identity.
Its private binding is `/etc/ptob/pi5/receiver.json`, mode 0600. Optional aliases
come from the shared private schema-1 registry and confer no authority. Dated
wire summaries are separate in `/etc/ptob/pi5/wire-checks.json`. Reinstallation
preserves these optional settings.

Tailscale uses its official signed Debian trixie repository. Authorize a
returned login URL if required, then repeat installation to enable private
Serve. The browser service binds `127.0.0.1:8080`; Serve supplies tailnet HTTPS.
No Funnel, exit node, subnet router or Tailscale SSH is configured. The desired
`pi.ptob.dev` custom domain requires DNS and TLS provisioning separately from
the automatic tailnet HTTPS name.

## Status and limits

“Complete” requires fresh bound receiver telemetry, a valid admitted clock
offer, recent GPIO PPS edges and chrony selecting `#* PTOB`. It does not certify
detector delay, parent accuracy, RF/HAT propagation or output UTC accuracy.
Receiver fit residuals measure internal consistency, not absolute error.

On source loss, PPS withdraws and the complete-path indicator clears. Chrony
can retain its previous source in holdover even at reach zero; the dashboard
labels this **PTOB holdover**, rather than live discipline or an NTP fallback.
See the [chrony FAQ](https://chrony-project.org/faq.html).

The bounded event journal carries source, boot, sequence, missed and overwritten
counts. The collector retains up to 128 received records per boot. Historical
control errors are distinct from current receiver faults; gaps are not rebuilt.

```sh
python3 gateways/pi5/tools/ntp_probe.py --out artifacts/ntp-wire.json
python3 gateways/pi5/tools/ptp_observe.py \
  --connection /absolute/private/pi-lan.json --out artifacts/ptp-wifi.json
```

The PTP unicast peer must match `timing-install --wifi-peer`. The observer sends
Delay_Req and a read-only management GET to open both UDP flows; it never adjusts
its clock. NTP responses alone do not establish host identity: correlate refid,
chrony state and requests arriving at the Pi. Networks can redirect UDP/123;
do not count another server's responses as gateway evidence. Tailscale provides
a private alternative over the same Wi-Fi.

Wi-Fi PTP uses software timing and an application-specific timescale without
UTC/traceability qualification. The onboard Wi-Fi driver lacks the TX timestamp
capability required by linuxptp software mode; this target uses PTPd, without
permission to adjust the system clock. Ethernet retains linuxptp hardware
timestamping, unknown accuracy/class 248 and a system-to-PHC transfer. Link-down
faults are expected without a cable. Ethernet accuracy and sub-microsecond
performance require independent reference measurement.

The [clock ownership/recovery guide](clock-chain.md) defines startup and loss
gates, kernel UTC/TAI handling, supervisor watchdogs and the current PHC's lack of
external PPS capture. `timing-status` includes the actual child/transfer state;
an active supervisor can be waiting with Ethernet serving withdrawn.

## Timing history and jitter diagnosis

The dashboard retains up to 86,400 one-second snapshots in a bounded local
SQLite history. It shows PTOB adjusted source offset, chrony last offset and
long-term RMS, the PTOB regression standard deviation, the receiver's internal
fit RMS and drift, and Pi GPIO PPS phase and period variation. These quantities
have different meanings: a small receiver residual cannot bound GPIO IRQ delay,
parent phase error or absolute UTC error. See the
[chronyc field definitions](https://chrony-project.org/doc/4.6/chronyc.html).

Inspect a chart with a pointer, touch or left/right arrow keys. Choose a window
from one minute through six hours. Window statistics include RMS, centered
standard deviation, absolute-value percentiles and the population of valid
snapshots. Null observations are excluded with their denominator visible.
Displayed lines preserve per-metric extrema when reduced; CSV export contains
all snapshots. Repeated estimates are snapshots, not independent measurements.
Chrony estimates remain visible between refclock updates while the PPS input is
fresh and the source is reachable; inspection shows the measurement age and poll
interval. An isolated valid observation is drawn as a point. Missing observations
remain gaps, including records previously stored without a value.
Receiver source/boot/path changes, admission changes, clock steps and observation
gaps break the lines. A withdrawn PPS interval is retained as an interval/gap,
not folded into the one-second period jitter population.

Absolute PPS timestamps are parsed as decimal integers before subtraction;
epoch-sized binary floating-point values would discard nanoseconds. GPIO PPS
phase is the IRQ timestamp relative to the nearest system UTC second. Period
variation includes chrony's system clock slew. Both remain software capture
observations. Live status lives under `/run/ptob-dashboard/`; history lives under
`/var/lib/ptob-dashboard/` and commits in ten-second batches. UART/PPS polling
continues while one background worker queries chrony. Service/PTP queries run
every fifteen seconds, rather than on every browser request.

The HAT planner commits a pulse only within twenty milliseconds of its second
boundary, using the latest valid admitted map. It preserves the minimum lead,
full-pulse validity, context withdrawal and hardware one-shot constraints.
Telemetry reports the last arm lead and map age. This limits extrapolation from
an older fit; it does not remove the Pi's GPIO interrupt timestamp latency.

The final compare selects the nearest representable timer edge instead of
always rounding after the boundary. The disciplined clock map retains its
fractional phase/rate estimate. A 62.5 ns output grid is not an uncertainty floor
for a mean, and the dashboard's non-overlapping 1–64 s block means retain
fractional nanoseconds. They use unique contiguous observed PPS edges, report
eligible block counts and lag-1 correlation, and preserve bias through separate
mean/RMS/standard deviation. They neither interpolate missed edges nor assume
independent samples. Host IRQ capture and chrony slew are part of this statistic.

The repository's [averaging discussion](../t096/round-trip-ranging.md#resolution-averaging-and-the-pps-witness)
explains why phase diversity and correlation matter; its ranging budget is not
a PPS error budget. The [software PLL evidence](../../evidence/papers/oscillator-pll-2026-10-03/README.md)
separates numerical clock-map discipline from physical oscillator discipline.
Historical [V14 phase results](../../evidence/v14/2026-10-12/README.md) evaluate
causal maps at witness edges, rather than this HAT's electrical PPS output.

`remote.py jitter-install` applies a separately bound host-capture profile:
GPIO16's dynamically discovered PPS IRQ runs on CPU 3, dashboard/collector
processes run on CPUs 0–2, and the CPU governor is `performance`. The profile
checks the exact factory serial/image and PPS IRQ identity on each boot.
This reduces avoidable contention and frequency transitions; it does not make
Linux GPIO capture a hardware PHC timestamp. To revert, disable
`ptob-pps-tuning`, restore the desired governor and PPS IRQ affinity, and remove
the dashboard units' `CPUAffinity` directives before redeploying them.

Repeated immutable owner polls do not invalidate a PPS planning snapshot. Only
semantic map/authority changes advance the adapter revision; saturation rejects
all new arms. Compare emitted and captured counter increments and exact pulse
intervals when diagnosing loss. A 2–3 s pulse interval is an availability gap,
not a jitter sample. A complete-path snapshot between pulses is not proof that
every nominal second produced a pulse.

For a bounded raw-tail capture, use the enrolled Pi's existing pinned SSH
profile. This reads the kernel's cached PPS assertion at 20 Hz on CPUs 0–2;
it does not change clock settings, read PCIe registers per pulse or open UART:

```sh
mkdir -p artifacts/pi5/reports
python3 gateways/pi5/tools/pps_capture.py --capture --seconds 300 \
  --out artifacts/pi5/reports/pps-tail.json
python3 gateways/pi5/tools/pps_capture.py \
  --analyse artifacts/pi5/reports/pps-tail.json \
  --out artifacts/pi5/reports/pps-tail-reanalysis.json
```

Choose unused report names. Raw source identities and timestamps stay in ignored
local artifacts. The second command is offline. Capture retains the initial
cached assertion as a seed, then reports new observed edges, P99, maximum and
strict counts above 1, 2 and 5 µs. It keeps the full phase population separate
from edges bracketed by eligible source observations. One-second source status
cannot prove exact-event lineage. Sequence skips identify assertions unseen by
the observer; long intervals between consecutive assertions identify nominal
pulse gaps, subject to system-clock steps. Neither is silently folded into
one-second period jitter. Empty populations remain unknown, and partial captures
retain errors. These remain software timestamp measurements, including IRQ
latency and system-clock slew, rather than independently measured output error.
