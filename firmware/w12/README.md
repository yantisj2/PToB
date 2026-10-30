# W12 · BLE receiver and holdover clock

PToB firmware for the **Meshnology W12**: receive authenticated mesh time,
inspect clock quality on an OLED and retain a local clock estimate through
short source gaps. Optional LoRa reception and an experimental BLE relay add
separate timing paths. W12 supports **RC (receiver/relay clock)**, with no GM
profile or USB timing stream.

[Build and set up](../../docs/w12/build.md) ·
[Understand holdover](../../docs/w12/holdover.md) ·
[Group setup](../../docs/GETTING_STARTED.md) · [Changelog](CHANGELOG.md)

<a href="../../docs/images/w12-rc-preview.png"><img src="../../docs/images/w12-rc-preview.png" width="384" alt="Illustrative W12 OLED showing time, selected relay parent and receiver diagnostics"></a>

*Historical illustrative layout. The current clock footer reports sampled P99
hold duration in seconds; the preview's older footer is not current display
instructions. [Image provenance](../../docs/images/README.md).*

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Hardware | Meshnology W12 ESP32-S3 board; **16 MiB flash** build configuration, **128 × 64** landscape monochrome OLED. Board profiles have fixed, reviewed pin routes. |
| BLE receiver | Authenticated **G1/RC7**, **BLE50** or **BLE100** listening. BLE50 is client-only; BLE100 can enable an estimated-TX relay. |
| Fine clock | Optional continuous **80 MHz counter**, **12.5 ns ticks**, timestamps rounded to a **50 ns grid**; W12-specific capture routing and calibration. |
| Holdover | BLE-only causal rate consensus and continuous entry/return. Local HOLD estimates do not extend authenticated source or relay permission. |
| Shadow diagnostics | **48** offset frozen clocks plus long probes; sampled P99 sub-1 µs duration. Temperature learning stays shadow-only. |
| Time zone | Front clock follows fresh GM time-zone/DST metadata, then falls back to UTC. LoRa/RC9 and timing diagnostics retain UTC. |
| Optional LoRa | Explicit LR2021 passive RC8 receiver; independent clock and separate RC9 functional UTC service with unknown bounds. Disabled by default. |
| Optional GNSS | Explicit L76K indicator/PPS-shadow profile with verified RX39/PPS41 wiring. Disabled by default; it does not train the normal BLE clock or make W12 a GM. |
| Host interface | Diagnostic console; no T096-compatible USB timing stream. Wi-Fi and PTS are disabled. |

**BLE50/BLE100 are listening profiles.** Choose BLE100 for the optional relay;
its transmit timing is estimated and has unknown RF/UTC bounds. Consult the
[transmit reference](../../docs/w12/tx-reference.md) before using downstream timing.

## Get started

1. **Confirm the board and optional modules.** Use the fixed W12 board profile;
   enable L76K or LR2021 only after verifying the physical wiring. A display
   alias or enclosure does not establish GNSS capability.
2. **Prepare the group.** Arrange a compatible T096 GM or relay, private trust
   enrollment, verified factory binding and the [shared registry](../../registry/README.md).
   Keep credentials separate from runtime discovery and authenticate afresh
   after a mesh context change. Current W12 uses the
   [coarse TZ wire](../../docs/esp32-time-zones.md); match the source's profile.
3. **Build and check offline.** Follow the [build guide](../../docs/w12/build.md)
   for public dependencies, BLE50/BLE100 selection, optional modules and tests.
   Enrolled builds contain secrets and stay in ignored local output.
4. **Install and inspect.** Follow the guide's exact-device app-only procedure
   with verified recovery and readback. Start near a compatible source, confirm
   the selected parent and fresh timing, then inspect clock quality and holdover.
   Optional relay operation needs its own observed validation.

## Screens and controls

Short button presses cycle the available clock, precision, holdover and radio
pages. Optional profiles determine which diagnostics are available.

| Action | Result |
| --- | --- |
| Hold **1.5 s** on Clock | Toggle automatic parent selection / relay-only reception. This selects the upstream policy, not relay transmission. |
| Hold **1.5 s** on LoRa | Cycle its receive-mode override. |
| Hold **4 s** on any non-clock page, then release | Request deep sleep after the original **2 s** owner drain and display shutdown. A new GPIO0 press reboots the application. |

A continued LoRa-page hold can request shutdown. Drain or power failures remain
faults; physical sleep current, battery lifetime and wake behavior need hardware
validation. See the [build guide](../../docs/w12/build.md) for complete controls.

## Read precision and holdover

**RMS** measures internal causal prediction quality, not independent UTC/output
accuracy. **P99 hold seconds** describes the supported duration at a sampled
absolute-error threshold of **1 µs**, with populations, failures and coverage
limits. `>=` is a lower bound; `--` means insufficient or stale evidence.
Witness gaps remain unmeasured, and overlapping trials are correlated.

Local HOLD preserves a BLE-trained estimate while ordinary source permission
expires independently. Temperature experiments never steer production HOLD.
Learned parents, models and retry history live in RAM; enrollment, names and
calibration are separate build inputs. Read the
[holdover guide](../../docs/w12/holdover.md) for the model, display and replay.

## Development and limits

[Holdover research and evidence](../../papers/mcxo-holdover-duration/README.md)
retain measured windows and their exact populations. Independent UTC/output
accuracy, calibrated relay delay, physical GM failover and continuous reliability
remain open. Host checks and native builds do not qualify later images or devices.
A generic public enrolled image and installer are not supplied.

| Explore further | Guide |
| --- | --- |
| Build, test and update | [Build guide](../../docs/w12/build.md) · [Fine-clock profile](../../docs/w12/build.md#fine-clock-test-profile) |
| Follow source recovery and fallback | [GM recovery](../../docs/w12/build.md#gm-successor-recovery) · [LR2021](../../docs/w12/lr2021.md) · [RC9](../../docs/w12/rc9.md) |
| Understand capture and relay paths | [Interactive ESP32-S3 timing schematic](https://ptob.dev/hardware/esp32-s3/timing/) · [Transmit reference](../../docs/w12/tx-reference.md) |
| Review changes and source origins | [Changelog](CHANGELOG.md) · [Import provenance](imports/2026-10-16.json) · [Cutover](../../docs/w12/cutover.md) · [Notices](NOTICE.md) |

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
Preserved dependency notices apply.
