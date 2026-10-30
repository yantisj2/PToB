# W12 native RC example

<a href="../../docs/images/w12-rc-preview.png"><img src="../../docs/images/w12-rc-preview.png" width="384" alt="W12 OLED preview showing time, relay parent and clock diagnostics"></a>

*Historical illustrative OLED layout. Current firmware reports sampled P99 hold
seconds; see the [holdover guide](../../docs/w12/holdover.md). RC2 indicates
relay depth. [Image provenance](../../docs/images/README.md).*

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns W12 native G1/RC7 development.** Current version **0.2.34** supports
**RC**: a BLE50 client or BLE100 receiver with an optional experimental
estimated-TX relay. It receives from the shared T096/W12/StickS3 mesh; it has no
GM profile or USB timing stream. Start with the
[build guide](../../docs/w12/build.md).

## Current behavior

- **BLE:** authenticated G1/RC7 receiver; optional BLE100 estimated relay.
  Channel-aware fitting, physical event matching, source expiry and controller
  ownership remain enforced. Estimated transmit timing has unknown RF/UTC bounds.
- **Holdover:** causal BLE rate consensus, continuous entry/return, 48 offset
  shadow clocks and sampled P99 hold duration. Temperature remains shadow-only.
  [Current model, display meaning and replay](../../docs/w12/holdover.md).
- **Resources:** full 256-entry receive history and exact estimator samples;
  checked 8192-byte static NimBLE host stack on the GNSS profile. Application
  watchdog intervals are at least five seconds; fault/no-time restart intervals
  remain 120/300 seconds. Sampling and transaction deadlines are separate.
- **Other inputs:** optional LR2021 passive RC8 and separate RC9 service;
  explicit L76K GNSS indicators/PPS shadow. Neither trains the normal BLE clock.
  GNSS defaults disabled; a board alias never establishes its wiring.
- **State:** learned parents, models and retry history are volatile. Enrollment,
  registry and calibration are build inputs. No learned-model flash persistence
  or generic distributable enrolled image is provided.

The [approved holdover paper](../../papers/mcxo-holdover-duration/README.md)
reports distinct sampled v33 windows and a v34 code audit. Its best window is
not acceptance of all devices, later images or source transitions. Independent
UTC/output, calibrated relay delay and physical GM failover remain open.

## Build and validate

Use the [build guide](../../docs/w12/build.md) for pinned public dependencies,
exact board profiles, registry/enrollment inputs, native checks and app-only
hardware safeguards. Routine publication cleanup does not operate devices.

| Guide | Contents |
| --- | --- |
| [Holdover](../../docs/w12/holdover.md) | Production policy, P99 seconds, shadow experiments and offline replay |
| [Fine-clock profile](../../docs/w12/build.md#fine-clock-test-profile) | Continuous counter, RX capture, fitting and calibration limits |
| [Transmit reference](../../docs/w12/tx-reference.md) | Programmed packet/ADDRESS endpoint and provisional offsets |
| [LR2021](../../docs/w12/lr2021.md) · [RC9](../../docs/w12/rc9.md) | Independent LoRa fallback, service/lease controls and tests |
| [GM recovery](../../docs/w12/build.md#gm-successor-recovery) | Authentication, scope reset, parent selection and recovery |
| [Changelog](CHANGELOG.md) | Version history and superseded behavior |

Short button presses cycle pages. Clock-page holds toggle relay mode at 1.5 s;
LoRa-page holds cycle RX mode. A four-second hold on a non-clock page requests
shutdown through the original bounded drain, then button release permits sleep.
See the build guide for controls and power-test limits.

## Timing schematic

[<img src="../../docs/images/esp32-timing-schematic.jpg" width="800" alt="ESP32-S3 / W12 receive, clock discipline and estimated relay paths">](https://ptob.dev/hardware/esp32-s3/timing/)

[Interactive schematic](https://ptob.dev/hardware/esp32-s3/timing/) ·
[image provenance](../../docs/images/README.md). The paper's final schematic
separately documents the current HOLD and continuous-shadow dataflow.

See [cutover](../../docs/w12/cutover.md), [import provenance](imports/2026-10-16.json),
[source notices](NOTICE.md), the [shared registry](../../registry/README.md) and
[group setup](../../docs/GETTING_STARTED.md). Private keys, factory bindings,
enrollment, raw captures and firmware images remain in ignored local artifacts.
