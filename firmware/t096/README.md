# T096 · GNSS, BLE mesh and USB clock

PToB firmware for the **Heltec T096 (HT-n5262G)**. Use it as a GNSS-backed
grandmaster, a BLE receiver/relay with a local clock display, or a dedicated
GNSS-to-USB source for macOS. Optional LoRa supplies a separate fallback timing
path.

**[Set up a group](../../docs/t096/gm-operations.md)** ·
[Build firmware](../../docs/t096/build.md) ·
[Troubleshoot](../../docs/t096/diagnostics.md) ·
[Changelog](CHANGELOG.md)

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 RC clock screen with digital UTC, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen. Displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

## Choose a profile

| Profile | What it does |
| --- | --- |
| **GM** | GNSS-backed grandmaster. G5 coordinates one serving **ACTIVE**, one appointed **HOT** successor and other **COLD** receivers. |
| **RC** | Receives authenticated BLE time from a GM or relay, displays it, and exports it over USB. Supports the RC7 relay path; following an RC2 stream stays a leaf. |
| **DC** | Dedicated GNSS-to-USB image with Bluetooth disabled and no enrollment. Includes a second GNSS serial/correction bridge. [DC guide](../../docs/t096/direct-connect.md). |

The ordinary mesh image switches between **GM and RC**. DC requires a separate
flash. ACTIVE/HOT/COLD are GM states; RC1/RC2 describe relay depth. A GM needs
local GNSS fix, live PPS and source qualification, including a 60-second dwell,
before serving time. COLD can receive BLE timing while qualifying locally.

## Get started

1. **Prepare compatible firmware.** Follow the [build guide](../../docs/t096/build.md)
   with your verified device identity. GM/RC devices need matching private group
   ID, PSK and key epoch; use a shared [name registry](../../registry/README.md).
   Keep enrolled images local; use the build guide's device-specific update path.
2. **Start the source.** Select GM and give GNSS a clear view of the sky. Check
   fix, PPS and source readiness on the quality/precision screens. For several
   GMs, establish an ACTIVE/HOT pair before adding COLD clocks; follow the
   [GM operating guide](../../docs/t096/gm-operations.md).
3. **Add receivers.** Select RC and start in range of the source. Check the actual
   selected parent, fresh timing and precision lock before adding another hop.
   Parent selection is automatic and considers timing delivery as well as signal
   strength; it searches again when usable time is lost.
4. **Connect USB if needed.** Use **GM → BLE → T096 RC → USB → macOS**, or
   **GNSS → dedicated T096 DC → USB → macOS**. Follow the
   [macOS client guide](../../clients/macos/README.md). USB timing is compiled
   into normal RC7 images; the normal GM backend does not publish a USB source map.

## Capabilities and specifications

| Area | Supported configuration |
| --- | --- |
| Hardware | Heltec T096 / HT-n5262G; Nordic **nRF52840**, **UC6580 GNSS** with PPS, **SX1262 LoRa** radio. |
| Display | **80 × 160** portrait color TFT; digital UTC, analog clock, RF activity, precision, GNSS quality, system and shadow-PPS pages; LoRa page in LoRa-enabled builds. |
| Display behavior | Normal refresh **4 Hz**, RF page **8 Hz**. When eligible, a moving screensaver starts after **5 minutes** idle at **50% brightness**. |
| BLE timing | Shared **G1/RC7** mesh with compatible T096, W12, StickS3 and N30 targets. Relay transmit support depends on the target/profile. |
| GM coordination | **G5 STATUS protocol 6**; authenticated source admission, finite successor grants, and independent local GNSS qualification. |
| LoRa timing | Optional **RC8 wire 3** fallback with root/source identity. BLE is preferred while usable; a configured transmitter can export qualified local GM time or its accepted locked BLE source. |
| USB | Hardware-captured USB frame boundaries; RC exports BLE time, DC exports local GNSS time. The macOS client uses an owned, unprivileged `chronyd -x` user clock. |
| Deployment | Source **0.15.166**, Zephyr-based native builds with pinned public dependencies. GM/RC uses private enrollment and verified device binding; no enrolled binary is distributed. |

LoRa is disabled by default and requires build-time configuration. GM mesh
participation can suppress passive RX. Fit the appropriate antenna before
enabling a transmitter. Fallback time has unknown total uncertainty and grants
no RC7 timing or GM takeover authority. See the
[LoRa contract](../../protocol/rc8/README.md) for configuration and limits.

## Button controls

| Action | Result |
| --- | --- |
| Short press | Next page; dismiss help. From the screensaver, the first press wakes the display. |
| Hold **1.5 s** on the main clock | Toggle GM ↔ RC in the ordinary mesh image. |
| Hold **1.5 s** on the LoRa page | Toggle the configured LoRa role's volatile ON/OFF request. This press is consumed, including a longer hold. |
| Hold **1.5 s** on another page | Show help. Continue to **4 s** to power off. |

The precision page separates stream RMS, local PPS continuity and ACTIVE/HOT
duration. Shadow-PPS compares received timing with local PPS when eligible.
These are relative timing statistics, not independent UTC accuracy measurements.

## Current limits

This is development firmware. Continuous BLE/USB availability, durable multi-GM
failover and calibrated UTC/output accuracy remain unqualified. Controller
failures and timing interruptions are retained in the
[GM field record and acceptance plan](../../docs/t096/gm-standby.md#development-checkpoint-and-remaining-acceptance).
Host tests and previews do not qualify hardware. A general public flasher and
runtime provisioning are still pending. The macOS client does not set the OS
clock or serve LAN time by default.

## Guides and development

| Task | Guide |
| --- | --- |
| Enroll and operate a group | [Group setup](../../docs/GETTING_STARTED.md) · [GM operations](../../docs/t096/gm-operations.md) |
| Build and inspect an image | [Build guide](../../docs/t096/build.md) |
| Investigate source selection, timing or faults | [Diagnostics](../../docs/t096/diagnostics.md) · [GM coordination](../../docs/t096/gm-standby.md) |
| Use GNSS directly over USB | [Direct Connect](../../docs/t096/direct-connect.md) · [macOS client](../../clients/macos/README.md) |
| Review releases and source origins | [T096 changelog](CHANGELOG.md) · [Source provenance](imports/README.md) · [Cutover record](../../docs/t096/cutover.md) |
| Explore experimental work | [PC awake timing experiment](../../docs/t096/power-control.md) · [Provisioning design](../../docs/t096/provisioning-design.md) |

The [interactive timing schematic](https://ptob.dev/hardware/t096/timing/)
illustrates capture, clock discipline and delivery paths. Historical papers and
experiments retain their own editions and limits in [papers](../../papers/README.md)
and [evidence](../../evidence/README.md).

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
See [source notices](NOTICE.md) for third-party terms.
