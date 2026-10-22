# N30 native RC target

| Clock · page 0 | BLE + LoRa · page 1 | LoRa receiver · page 2 |
| :---: | :---: | :---: |
| [<img src="../../docs/images/n30-clock-preview.png" width="192" alt="N30 clock dashboard with UTC, analog dial, source and relative synchronization RMS">](../../docs/images/n30-clock-preview.png) | [<img src="../../docs/images/n30-rf-preview.png" width="192" alt="N30 dual-radio page with separate BLE and LoRa clocks, activity, time difference and drift">](../../docs/images/n30-rf-preview.png) | [<img src="../../docs/images/n30-lora-preview.png" width="192" alt="N30 LoRa receiver page with RX mode, time, link fit, root, parent and age">](../../docs/images/n30-lora-preview.png) |

*Illustrative screens from the production renderer, using synthetic values.
Select a screen for its native 64×128 image.
[Image provenance](../../docs/images/README.md#n30-portrait-previews).*

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns N30 native G1/RC7 development.** Version **0.2.5** supports an **RC**
with **two timing inputs: BLE and passive LoRa**, a portrait OLED clock and a
receiver-status console. The Meshnology N30 / Heltec WiFi LoRa32 V3 route uses
an ESP32-S3, 8 MB flash and a 128×64 OLED mounted as 64×128. Start with the
[build and onboarding guide](../../docs/n30/build.md).

BLE100 reception is standard; BLE50 is client-only. An optional BLE100 relay
uses experimental estimated transmit timing with unknown RF/UTC bounds. The
N30 has no GM profile or GNSS input. Its UART console is not a T096 USB timing
source. WiFi stays disabled.

## BLE and LoRa timing

The N30 keeps **independent BLE and LoRa clocks**. BLE supplies the ordinary
G1/RC7 mesh clock; passive RC8 wire3 LoRa from an explicitly configured T096
transmitter provides display fallback. A valid, settled BLE relative lock has
display preference. When BLE is marginal or pending and LoRa time is available,
the clock dashboard uses `LOR`, keeps its analog dial and PPS lamp, and labels
its quality `LINK RMS`.

LoRa **AUTO** starts reception after **180 seconds without live BLE lock** and
stops it after **180 seconds of continuous BLE lock**. A lock dip restarts the
recovery interval. The LoRa driver is receive-only, using the fixed 915 MHz
SX1262 configuration, shared group authentication and exact transmitter identity.
LoRa hardware stays disabled unless the build binds a transmitter from the shared
registry. Its 12-second functional hold does not establish source freshness. LoRa never
trains the BLE clock or grants downstream timing authority.

The dual-radio page shows each clock separately, BLE and LoRa activity, `DNS`
(BLE minus LoRa nanoseconds) and drift in `PPB`. Comparison requires matching
full root identity, root boot and source epoch, with current LoRa companion
metadata. Unavailable or mismatched inputs show dashes. These are relative
estimates; display refreshes do not create new comparison samples or qualify
absolute UTC accuracy. See the [LoRa build and timing details](../../docs/n30/build.md#version-023-passive-lora-receiver).

## Screens and controls

A short press cycles **clock → dual radio → LoRa → clock**. The first press
while blanked or in the saver wakes the screen and is consumed.

| Screen | What it shows | Hold action |
| --- | --- | --- |
| Clock | UTC, admitted source, analog clock, relative RMS, reception mode and bars | 1.5 s toggles automatic upstream selection / relay-only reception. This does not enable relay transmission. |
| Dual radio | Independent BLE/LoRa clocks, producer activity, relative difference and drift | 1.5 s opens RF help; 4 s blanks the display while timing continues. |
| LoRa | RX state, AUTO/manual policy, time, link fit, root/parent and age | 1.5 s overrides AUTO to the opposite RX state; KEEP ON → KEEP OFF → AUTO. |

`BLE`, `RC1` and `RC2` describe the admitted BLE upstream path; `LOR` identifies
the LoRa display fallback. The clock footer shows `R` for relay-only reception
and `>` during a pending mode change. Holds produce one action per press;
LoRa overrides are volatile and boot into AUTO.

The clock and LoRa pages refresh at 4 Hz; the dual-radio page at 8 Hz. Clock
frames follow the modeled UTC phase. The PPS lamp is a 250 ms display pulse
with a live timing gate, not a physical PPS output. A 500 ms presentation grace
holds the last displayed clock through a brief quality dip, with PPS suppressed
while the live gate is lost. Timing validity and radio authority retain their
actual expiry throughout that visual grace.

## OLED screensaver

After five idle minutes, a currently locked clock enters a moving saver at
half the normal OLED contrast. Normal saver refresh remains 4 Hz. After
15 minutes of actual saver activity, only the moving UTC/source header and
250 ms PPS lamp remain, with no routine rendering or I2C refresh during the
remaining 750 ms of each second. Positions advance every 30 seconds.

Both layouts share one saver session and its 15-minute timer. BLE/LoRa source
switches, a precision-lock dip with usable following time, and a reset of the
pixel cache keep that session running. The saver exits only after 500 ms with
neither live following BLE time nor available LoRa time, or an explicit user
wake/navigation action. Local HOLD and stale snapshots do not count as live
synchronization. The PPS lamp keeps its existing live-lock gate.

Minimal mode blocks the display task between timer, model and button events;
unused legacy indicator calculations pause. BLE/LoRa workers and source expiry
continue. The continuous XTAL timing counter retains its chip light-sleep lock.
Half contrast is a controller setting, not a measured luminance or battery-life
ratio. Power draw and long-duration saver behavior require separate observations.

## Build, identity and verification

The [build guide](../../docs/n30/build.md) covers pinned public ESP-IDF/toolchain
setup, offline tests/previews, BLE50/100 and optional relay builds, and passive
LoRa configuration. The target compiles the actual W12 timing core and selected
T096 RC8 codec/policy sources; it owns its display, UART and RX-only radio adapters.
See [source notices](NOTICE.md), [entry-point provenance](imports/2026-10-20.json)
and [LoRa provenance](imports/2026-10-20-lora.json).

N3-1 and N3-2 use the [shared display registry](../../registry/README.md).
Factory identities, real maps, group enrollment and enrolled images remain
ignored local inputs. Onboarding requires an independently verified factory MAC;
an alias is not device identity or installation permission. No enrolled binary
is distributed. Default builds remain `OFFLINE_NOT_FLASHABLE`; commissioning
requires clean committed sources and a separate exact-device hardware review.

Host/sanitizer tests, native builds and illustrative screens establish software
properties. Finite console/display/reception checks do not qualify sustained
interoperability, 180-second physical AUTO dwell, estimated relay accuracy,
failover or independently measured UTC/PPS accuracy. Preserve failures and
missing intervals separately. See the [project changelog](../../CHANGELOG.md)
and [build guide](../../docs/n30/build.md) for development history and limits.
