# T096 Direct Connect (DC)

Copyright (c) 2026 Jonathan Yantis. MIT.

DC is a separate, flash-only T096 image for a dedicated **GNSS → USB → macOS**
clock. It does not join a group, scan, advertise, connect, elect a GM, receive
mesh time or relay timing. Bluetooth is disabled in Kconfig; the mesh/controller
owners and runtime GM/RC switch are absent from the linked image. GM and RC
remain the ordinary mesh profiles in their common image. Flashing a different
image is required to enter or leave DC.

The device retains its digital/analog clock, PPS indicator, temperature,
screensaver, button navigation and supported shutdown/maintenance path. The
front face identifies **DC** and replaces **BEAT RMS** with **USB RMS**.
The dial shows the standard USB client's **32 Hz** sampling rate, retaining
the existing temperature colors (blue below 32 °C, green through 39 °C, amber
above 39 °C, dim when temperature is unavailable). This is the nominal client
sampling rate, not a count of hardware SOFs or a measured delivery rate.
Diagnostic pages describe GNSS/USB instead of mesh/HOT status. Holding the
button cannot select GM or RC; a console profile request returns unsupported.

## Timing and validity

The existing GNSS parser and discipline owner pair local RMC/PPS observations.
Hardware PPS captures discipline a continuous source map on the HFXO-backed
16 MHz TIMER3. DC translates each already available, immutable source map into
the shared DC/RC USB UTS1/UTE1 publisher, including its phase slew. Repeated
owner polls of the same immutable map do not invalidate in-flight serialization
or renew the original map/source/session deadlines. Its integer ppb
representation differs from the Q16 source projection by at most 3 ns in the
tested synthetic population; this is arithmetic agreement, not measured output
accuracy. The ±300 ppm macOS map contract is retained. Larger acquisition slews
withhold USB observations until an admissible map is available.

USBD hardware SOF capture uses TIMER3 CC3, PPS uses CC0 and current-counter
reads use CC2. DC allocates no RADIO capture channel. The capture owner keeps
its existing continuity checks and HFXO ownership. The local silicon identity,
boot, GNSS source epoch, capture epoch, map generation and domain tag
`DC-GNSS-USB-v1` identify the source. The USB root is the attached device itself;
there are zero BLE relay hops. The domain tag describes local provenance; it is
not a BLE authentication proof or an accuracy certificate.

Publication requires a valid local GNSS clock, active fix, live PPS,
matching UTC/source/capture epochs and a usable PPS-disciplined precision map.
The raw clock's `source_available` bit additionally requires independently
commissioned PPS/UTC labeling and stays false with unknown association. It is
not DC's publication permission. `status.dc_source_active` reports that separate
GNSS/USB map state; USB availability never sets the raw qualification bit.
Its deadline is clipped to the original source map, RMC freshness (2.5 s) and
PPS freshness (1.5 s), with no renewed permission from USB polling. A stale
owner snapshot is rejected. New maps take effect only after publication; an old
SOF cannot use a later fit. GNSS withdrawal fences strict observations promptly,
and USB reset/suspend/reconnect fences the USB epoch. The existing EP0 recovery
and power-event replay fixes remain included.

UTE1 may still report a retained **local estimate** after quality withdrawal,
with its original observation age and expired session/source indicators. It
does not become a fresh chrony measurement. UTS1 valid observations remain
withheld. A new source epoch or capture incarnation fences prior training.

GNSS PPS-to-UTC-second association remains **unknown** on this board adapter.
UTC is the existing `POSIX_UTC_ESTIMATE`, using a retained coarse RMC translation.
Error, rate and capture bounds remain unknown. Removing BLE hops does not qualify
independent UTC accuracy, USB/host bias, uninterrupted delivery or long-term
durability. A build and software tests cannot establish those hardware outcomes.

## GNSS serial and correction transport

The DC development image adds a second CDC ACM function for the UC6580 serial
stream. The original diagnostic console and vendor precision interface retain
their roles. The new function is compiled only through `dc.overlay` and the DC
source list; GM and RC retain their existing GNSS byte path and USB layout.
Select the GNSS function by the full device serial and USB interface ancestry,
not by assuming a persistent `/dev/cu.usbmodem` suffix. Serial clients must not
compete to read the same function. Maintenance requires all clients of the unit
to release their interfaces.

Receiver output is copied unchanged into a bounded queue. The clock input uses
a separate DC filter for RMC/GGA and aiding feedback; RTCM payloads and unrelated
long sentences cannot become clock input. Target timing faults and original
freshness deadlines remain enforced. Slow readers lose raw bytes rather than
blocking PPS or the source owner. Application backlog expires after 500 ms;
USB/driver buffers and plain NMEA do not provide an end-to-end freshness proof.
Clock diagnostics add `dc_gnss_bridge`: raw-output drops, host-input drops,
rejected host frames, dropped commands, and queued raw-output bytes.

Host output accepts complete CRC24Q-valid RTCM frames and a narrow command set:
product/configuration queries, enabling standard navigation sentences, NMEA 4.1,
and an explicit fixed-115200 UART1 configuration retaining Unicore/NMEA while
enabling RTCM input/output. PPS changes, receiver reset, persistent writes,
arbitrary commands and baud changes are refused. USB line coding never changes
the physical GNSS UART. UART TX remains owned by the GNSS adapter, mutually
exclusive with aiding; epoch changes, timeout and shutdown request bounded abort,
and power-off waits for actual DMA return. Queued commands retain a 250 ms
original ingress deadline.

This enables receiver configuration and correction/raw-data experiments; it does
not add an onboard RTK solver. UC6580 raw output depends on the installed GNSS
firmware. Query product information before enabling it. Firmware replacement,
RTK fix performance, physical serial coexistence and calibrated UTC remain
separate validation. Offline checks:

```sh
CC=clang python3 firmware/t096/tools/dc_gnss_tests.py --out artifacts/check-dc-gnss
```

## USB RMS

USB RMS measures **device-observed USB SOF cadence against the GNSS-disciplined
source map**. For two distinct observed SOFs in the same immutable map and
USB/capture/source epochs, it computes:

`error_ns = floor((mapped_ns_new - mapped_ns_old) / frame_span) - 1,000,000`

The displayed value is the root mean square of up to 32 such per-frame span
errors, without removing their mean or fitting them again. At least four spans
and an observation younger than one second are required. Frame-number wrap is
handled; spans of one second or more, duplicates and invalid observations cannot
train it. A generation/epoch change starts a new population. Gaps are observed
spans, not claimed individual frame samples. Invalid requests/USB lifecycle
changes clear the population. The value is `--` without sufficient fresh data.

This is neither macOS/chrony estimator RMS nor end-to-end UTC error. It depends
on USB polling, the host SOF cadence and the GNSS map. The console `precision`
query names this metric and reports sample count/validity explicitly.

The macOS client's **RX USB RMS** separately measures corrected-source causal
prediction errors at the receiver, before chrony. Run it with `--no-chrony
--no-ntp` for a USB-only baseline; [the client guide](../../clients/macos/README.md)
defines the prior-sample fit and its counts/gates. Keeping that client polling
also supplies the observations needed by the device's TX display metric.

## Build and package

Use the pinned dependencies and exact supported hardware/device JSON described
in the [build guide](build.md). DC reads **no group enrollment or PSK**; passing
`--enrollment` with DC is rejected. Real device binding stays ignored locally.
The default registry is the synthetic public example; DC has no peer roster.

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/build.py \
  --deps artifacts/deps --out artifacts/build-dc-01 --profile DC \
  --device-config artifacts/local/device.json
artifacts/deps/venv/bin/python firmware/t096/tools/inspect_build.py artifacts/build-dc-01
CC=clang python3 firmware/t096/tools/dc_tests.py --out artifacts/check-dc-01
artifacts/deps/venv/bin/python firmware/t096/tools/package_dc.py \
  --build artifacts/build-dc-01 --out artifacts/package-dc-01
```

Packaging requires a clean committed build and a passing inspector. It includes
the app-only UF2/BIN, source snapshot, dependency lock, build/inspection receipts,
exact device binding, licensing material and checksums. It never opens hardware.
Treat the exact-device package as a local validation candidate. Use the reviewed
identity/ownership/recovery/protected-region update process from the build guide;
packaging does not authorize a flash or bypass its guards.

The existing [macOS USB client](../../clients/macos/README.md) consumes the same
wire contract; its offline tests include a DC local-root map. Normal launch
opens USB and may start the owned chronyd. The historical `BLE` chrony refid is
a client label and does not insert a BLE hop. No client launch is part of a DC
build or package.

A future website offer must separately close generic-device identity/provisioning,
public install/recovery, USB VID/PID and binary redistribution review, and bind
durability/precision claims to actual hardware evidence. This exact-device
candidate is not a generic downloadable release. No website deployment is part
of this work.
