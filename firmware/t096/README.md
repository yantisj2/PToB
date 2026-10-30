# T096 · GNSS, BLE mesh and USB clock

PToB firmware for the **Heltec T096 (HT-n5262G)**. Use it as a GNSS-backed
grandmaster, a BLE receiver/relay with a local clock display, or a dedicated
GNSS-to-USB source for macOS. Optional LoRa supplies a separate fallback timing
path.

[Set up a group](../../docs/GETTING_STARTED.md) ·
[Build firmware](../../docs/t096/build.md) ·
[Troubleshoot](../../docs/t096/diagnostics.md) · [Changelog](CHANGELOG.md)

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 relay-clock preview with digital time, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen. Displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

## Choose a profile

A **grandmaster (GM)** supplies reference time. A **relay clock (RC)** follows
received time and can make it available to downstream clients.

| Profile | What it does |
| --- | --- |
| **GM** | GNSS-backed grandmaster. One serving **ACTIVE** nominates a preferred **HOT** successor; standbys arm locally after independent qualification and physical shadow preparation. |
| **RC** | Receives authenticated BLE time from a GM or relay, displays it and exports it over USB. Supports the RC7 relay path; following an RC2 stream stays a leaf. |
| **DC** | Dedicated GNSS-to-USB image with Bluetooth disabled and no enrollment. Includes a second GNSS serial/correction bridge. [DC guide](../../docs/t096/direct-connect.md). |

The ordinary mesh image switches between **GM and RC**. DC requires a separate
flash. ACTIVE/HOT/COLD are GM states; RC1/RC2 describe relay depth. A GM needs
local GNSS fix, live PPS and source qualification, including a **60-second dwell**,
before serving time. COLD can receive BLE timing while qualifying locally.

## Get started

1. **Prepare compatible firmware.** Follow the [build guide](../../docs/t096/build.md)
   with verified device identity, private trust enrollment and a shared
   [name registry](../../registry/README.md). Keep trusted credentials separate
   from local mesh discovery; a context change requires fresh authentication.
   Enrolled images stay local. Use matching coarse-wire profiles across the group.
2. **Start the source.** Select GM and give GNSS a clear view of the sky. Check
   fix, PPS and source readiness on the quality/precision screens. For several
   GMs, follow the [GM coordination guide](../../docs/t096/gm7-implementation.md)
   and allow the nominated standby to finish local shadow preparation.
3. **Add receivers.** Select RC in range of the source. Check the actual selected
   parent, fresh timing and relative clock lock before adding another hop.
   Automatic parent selection considers delivery as well as signal strength
   and searches again when usable time is lost.
4. **Connect USB if needed.** Use **GM → BLE → T096 RC → USB → macOS**, or
   **GNSS → T096 DC → USB → macOS**. Follow the
   [macOS guide](../../clients/macos/README.md). Normal RC images include USB
   timing; the normal GM backend does not publish a USB source map.

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Hardware | Heltec T096 / HT-n5262G; Nordic **nRF52840**, **UC6580 GNSS** with PPS, **SX1262 LoRa** radio. |
| Display | **80 × 160** portrait color TFT; digital/analog clock, RF activity, precision, GNSS quality, system and shadow-PPS pages; LoRa page when enabled. |
| Display behavior | Normal refresh **4 Hz**, RF page **8 Hz**. Eligible screensaver starts after **5 minutes** idle at **50% brightness**. |
| BLE timing | Authenticated **G1/RC7** precision reception and relay. Current coarse TZ wire uses **GM7 revision 4, JOIN revision 5, RC control/capability revision 8**. |
| GM coordination | Broadcast ANN/STATUS/ROLE, persistent G1 following without GM admission, local HOT readiness and independent GNSS qualification. Client GATT admission remains required. |
| LoRa timing | Optional **RC8 wire 3** fallback with root/source identity. A configured transmitter can export qualified local GM time or its accepted locked BLE source. |
| USB | Hardware-captured USB frame boundaries; RC exports BLE time, DC exports local GNSS time to the macOS client. |
| Time zone | GM uses its GNSS location and pinned IANA offset calendars; manual-zone/fixed-offset overrides. Local display follows leased metadata and returns to UTC on expiry. |
| Development | Zephyr-based native builds with pinned public dependencies; private device binding and GM/RC enrollment. No enrolled binary is distributed. |

W12, N30 and CoreS3-SE consume the current coarse TZ profile; StickS3 retains
its own selected profile. Check each target's guide before pairing images.
Precision payload names alone do not establish coarse-wire compatibility.
[Wire contract](../../protocol/time-zone-v1/README.md) ·
[W12/N30 TZ behavior](../../docs/esp32-time-zones.md) ·
[Automatic time zones and DST](../../docs/t096/auto-time-zone-plan.md).

LoRa is disabled by default and needs build-time configuration and the
appropriate antenna before transmission. BLE is preferred while usable; GM
mesh participation can suppress passive LoRa RX. Fallback has unknown total
uncertainty and grants no RC7 timing or GM takeover authority. See the
[LoRa contract](../../protocol/rc8/README.md).

## Button controls

| Action | Result |
| --- | --- |
| Short press | Next page; dismiss help. The first press from the screensaver wakes the display. |
| Hold **1.5 s** on the main clock | Toggle GM ↔ RC in the ordinary mesh image. |
| Hold **1.5 s** on the LoRa page | Toggle the configured LoRa role's volatile ON/OFF request. This consumes the press, including a longer hold. |
| Hold **1.5 s** on another page | Show help. Continue to **4 s** to power off. |

The precision page separates stream RMS, local PPS continuity and ACTIVE/HOT
duration. Shadow-PPS compares received timing with local PPS when eligible.
These are relative timing statistics, not independent UTC accuracy measurements.
Clock calculations, USB timestamps and diagnostics stay UTC.

## Development and limits

Continuous BLE/USB availability, durable multi-GM failover and calibrated
UTC/output accuracy remain unqualified. Current acceptance records retain
passing direct-path windows, failed relay availability and incomplete successor
gates in the [GM acceptance plan](../../docs/t096/gm-standby.md#development-checkpoint-and-remaining-acceptance)
and [TZ validation record](../../docs/t096/time-zone-test-plan.md).
Host tests and previews establish software behavior only. Public installation
and runtime provisioning remain pending; hardware work needs exact-device scope,
verified recovery and the build guide's update checks.

| Explore further | Guide |
| --- | --- |
| Build and inspect an image | [Build guide](../../docs/t096/build.md) |
| Investigate source selection or faults | [Diagnostics](../../docs/t096/diagnostics.md) · [GM coordination](../../docs/t096/gm7-implementation.md) |
| Use GNSS directly over USB | [Direct Connect](../../docs/t096/direct-connect.md) · [macOS client](../../clients/macos/README.md) |
| Review changes and source origins | [Changelog](CHANGELOG.md) · [Import provenance](imports/README.md) · [Cutover](../../docs/t096/cutover.md) · [Notices](NOTICE.md) |
| Explore the architecture | [Interactive timing schematic](https://ptob.dev/hardware/t096/timing/) · [Papers](../../papers/README.md) · [Evidence](../../evidence/README.md) |
| Explore proposed features | [PC awake timing experiment](../../docs/t096/power-control.md) · [Provisioning design](../../docs/t096/provisioning-design.md) |

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
Preserved dependency notices apply.
