# PToB — Precision Time over BLE

PToB explores shared time over Bluetooth Low Energy periodic advertising, with
explicit source validity, clock quality and availability. This repository is the
home for its firmware, macOS client, draft papers and selected experimental
evidence. [Project website](https://ptob.dev).

**Start with the main paper: [PToB Viability: Rooted BLE Timing Meshes for
Heterogeneous Edge Networks](papers/ptob-viability-preprint.pdf)** — research
draft, 16 October 2026. It brings together the architecture, experimental
evidence, scaling limits and proposed path to a PTP edge extension.
[Supporting research drafts and paper sources](papers/README.md).

**Latest public review draft, 26 October 2026:**
[Reducing Timing Tails and Multipath Sensitivity in an Operating BLE Precision Mesh](papers/ble-tail-multipath/main.pdf)
reviews timing mechanisms and proposes practical placement, channel, workload
and recovery tests. [Source, supplement and verification](papers/ble-tail-multipath/README.md)
accompany the nine-page draft. Historical timing tails remain unexplained;
no new hardware experiment or multipath-cause attribution is claimed.

## Get started

Follow the **[setup guide](docs/GETTING_STARTED.md)** to plan two grandmasters
(GMs), optional relay clocks, and a USB-connected macOS client with chrony.
A grandmaster supplies reference time; a relay clock (RC) follows the network.
Start with the target matching your hardware:

| Target | Current role | Start here |
| --- | --- | --- |
| Heltec T096 | GM/RC mesh clock or separate DC GNSS/USB image | [T096 README](firmware/t096/README.md) |
| Meshnology W12 | Receiver; optional experimental relay with estimated transmit timing | [W12 README](firmware/w12/README.md) |
| M5Stack StickS3 (SS3) | Receiver and experimental relay with estimated transmit timing | [SS3 README](firmware/ss3/README.md) |
| Meshnology N30 | BLE RC with passive LoRa timing fallback and portrait OLED | [N30 README](firmware/n30/README.md) |
| M5Stack CoreS3-SE | Precision diagnostics instrument: receiver quality, mesh lineage and GM reference status | [CoreS3-SE README](firmware/corese/README.md) |
| macOS | USB time client and chrony bridge | [macOS README](clients/macos/README.md) |

For T096 GMs, follow the **[current GM7 guide](docs/t096/gm7-implementation.md)**
for ACTIVE/HOT/COLD behavior and compatibility. Source 0.15.206 adds
[GM location-based TZ and automatic DST](docs/t096/auto-time-zone-plan.md)
using the existing four-byte coarse metadata and local clock display;
[W12 0.2.19 and N30 0.2.36](docs/esp32-time-zones.md) consume these dynamic TZ updates;
[full T096 certification remains incomplete](docs/t096/time-zone-test-plan.md#local-205-results-29-october-2026).

See the [changelog](CHANGELOG.md) for recent development updates.

**Developer preview — build from source:** a generic downloadable image,
public installer/recovery flow and mutable provisioning are not yet available.
Firmware images use private build-time enrollment and must not be uploaded as releases.
Use the target guides for exact supported boards, dependencies and validation
limits. Hardware observations and passing host tests do not establish calibrated
UTC accuracy or qualified failover.

One group uses one private pre-shared key ([PSK enrollment](docs/security/psk.md)) and one
[shared display registry](registry/README.md). Read the
[device ID / name / UUID rules](docs/security/identity-and-names.md) before
provisioning. The [T096 provisioning design](docs/t096/provisioning-design.md)
describes the proposed future storage/API; it is not implemented firmware.

## See the clocks

| T096 · relay clock | W12 · relay clock | StickS3 · network health | N30 · BLE + LoRa clock |
| :---: | :---: | :---: | :---: |
| [<img src="docs/images/t096-rc-preview.png" width="120" alt="T096 preview with digital time, analog dial and BLE source status">](firmware/t096/README.md) | [<img src="docs/images/w12-rc-preview.png" width="192" alt="W12 OLED preview with time, relay parent and timing diagnostics">](firmware/w12/README.md) | [<img src="docs/images/ss3-health-preview.png" width="135" alt="StickS3 preview with network delivery, signal and source health">](firmware/ss3/README.md) | [<img src="docs/images/n30-clock-preview.png" width="96" alt="N30 portrait OLED clock dashboard with UTC, analog dial and BLE source">](firmware/n30/README.md) |

Firmware-rendered previews with illustrative values, not live measurements.
Select a screen to open its target guide.

[N30 clock, dual BLE/LoRa, Precision and receiver screens](firmware/n30/README.md) show its
independent timing inputs and passive LoRa display fallback. N3-1/N3-2 onboarding
requires verified factory identities; hardware qualification remains separate.

For USB timing, ChronyControl tracking and client development updates, see the
[macOS client README](clients/macos/README.md).

## Bring your own target

The **[porting guide](docs/PORTING.md)** covers BLE periodic advertising,
timestamp association, identity, enrollment and acceptance checks. Hardware
oscillator tuning and GNSS are optional; the source and timing requirements
depend on the role. Conventional clients can consume a host time service without
implementing a BLE mesh role. A general GATT time-client service remains planned.

## Research and review

[Read the draft papers](papers/README.md), [inspect the evidence](evidence/README.md),
or follow the [architectural background](docs/V12_TO_R2.md). Paper editions,
wire profiles and current firmware versions have separate identities. Historical
results do not qualify later builds. PToB is not an adopted Bluetooth SIG or IEEE
standard.

**Public review draft, 26 October 2026:**
[PToB-IPS: Toward GNSS-like Indoor Positioning with Surveyed BLE Timing Beacons](papers/indoor-ips/main.pdf)
explores a local constellation of surveyed timing beacons, with aligned periodic
advertising offsets/rates and a GNSS-like position/clock solve. It quantifies
averaging, bias, finer capture clocks and motion limits for future MCU robotics
and drone PNT. [LaTeX and verification](papers/indoor-ips/README.md) and
[publication approval](papers/editions/indoor-ips-2026-10-26-publication.json)
accompany the theoretical draft; positioning and IPS implementation remain
unqualified.

**Public review draft, 22 October 2026:** [A Shared Time Base for Robotic gPTP and
TSN Systems](papers/ptob-gptp-tsn-robotics-preprint.pdf) investigates extending
a wired robot time domain to wireless acquisition endpoints, with a proposed
PHC-to-radio bridge, ROS 2 adapters and independent validation programme.
[LaTeX source](papers/ptob-gptp-tsn-robotics-preprint.tex) and
[edition manifest](papers/editions/gptp-tsn-robotics-2026-10-22.json) preserve its
scope; gPTP interoperability, TSN conformance and physical actuation remain
unqualified.

**Follow-up research draft, 22 October 2026:** [Precision Bluetooth Timing on
ESP32-S3: Causal Clock Discipline and Hardware Event Capture](papers/ptob-esp32s3-timing-preprint.pdf)
reports the completed 660-second experiment: **166.590 ns relative PPS RMS**
across 657 eligible and three invalid records, with zero nominal missing and
all tails retained. A separate N3-1 transfer measured **150.954 ns RMS** across
48 eligible of 64 records. The observed 49 ns internal display RMS is a
different metric; the sub-100 ns downstream target remains open.
[LaTeX source](papers/ptob-esp32s3-timing-preprint.tex),
[edition manifest](papers/editions/s3-timing-2026-10-22-fine.json) and
[selected evidence](evidence/s3/2026-10-22-fine/README.md) preserve the measured
scope, earlier failures and remaining TX-event/RX-delay validation work.

**Public review draft, 22 October 2026:** [A Shared Time Base for Bluetooth
Timecode Ecosystems](papers/ptob-bluetooth-timecode-preprint.pdf) explores PToB
as a shared timing source with proposed device-specific compatibility layers.
[LaTeX source](papers/ptob-bluetooth-timecode-preprint.tex) and
[edition manifest](papers/editions/bluetooth-timecode-2026-10-22.json) accompany
the paper; commercial interoperability and physical capture timing remain
unqualified.

For review, identify the target and commit or paper edition, the behavior or claim,
and a reproducible check. Keep credentials and raw private captures out of issues.
See the [publication review checklist](docs/PUBLICATION_REVIEW.md) for remaining
release gates.

By **Jonathan Yantis**, with [acknowledged assistance](CREDITS.md). Original code
and ordinary documentation use [MIT](LICENSE); papers, figures and original
evidence datasets use CC BY 4.0. Preserved third-party terms also apply:
[licensing and attribution](LICENSING.md).
