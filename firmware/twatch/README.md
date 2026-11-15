# T-Watch Ultra · precision instrument watch

PToB firmware for the **LILYGO T-Watch Ultra V1 with SX1262**: a touchscreen
watch with a Casio-inspired Digital face and an olive mechanical Instrument
face. Both follow authenticated BLE mesh time, with passive LoRa kept ready as
a fallback. The watch boots into its last selected face.

[Build and set up](../../docs/twatch/build.md) ·
[Group setup](../../docs/GETTING_STARTED.md) ·
[Power plan](../../docs/twatch/power.md) · [Changelog](CHANGELOG.md)

| Digital | Instrument |
| :---: | :---: |
| [<img src="../../docs/twatch/images/digital.png" width="205" alt="Digital watch face with segmented time, source, Sync RMS, PLL lock and P99 hold duration">](../../docs/twatch/images/digital.png) | [<img src="../../docs/twatch/images/instrument.png" width="205" alt="Olive Instrument face with pilot hands, source aperture, PLL phase well, RMS and hold subdials, and battery reserve">](../../docs/twatch/images/instrument.png) |

*Native production-renderer screenshots with synthetic inputs, not hardware
photographs or timing measurements. [Image provenance](../../docs/twatch/images/README.md).*

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Hardware | LILYGO T-Watch Ultra V1, **ESP32-S3**, **16 MiB flash** configuration and PSRAM-backed rendering. **SX1262** radio; SX1280 variants are unsupported. |
| Display | **410 × 502 CO5300 AMOLED**, CST9217 touch, corner-safe face layouts and adjustable brightness. |
| Faces | Digital instrument display and mechanical Instrument with pilot hands, reception apertures, PLL phase, RMS/hold subdials and battery reserve. |
| BLE | Authenticated **G1/RC7** receiver at **BLE100**, using the shared W12/N30 backend. Nearby mesh discovery and fresh admission remain separate from trusted enrollment. |
| LoRa | Passive SX1262 fallback using N30 source selection. Reception stays warm with a configured compatible transmitter; BLE is preferred. |
| Clock | **20 MHz** baseline backend. No transplanted W12/N30 fine-capture wiring or calibration. Authenticated time-zone metadata supplies the display offset. |
| Settings | Persistent face, brightness and display timeout. **90 seconds** by default; **120** and **180 seconds** are available. |
| USB | Native Serial/JTAG diagnostics and bounded software ROM entry for maintenance. Not a T096 USB timing source. |
| Power | GNSS is powered off. Wi-Fi, NFC, audio, SD and motion processing are not started. Screen sleep leaves timing reception and USB running. |

## Get started

1. **Verify the watch and radio.** Confirm Ultra V1, SX1262 and the factory MAC.
   The USB serial alone does not identify the radio variant. Follow the
   [build guide](../../docs/twatch/build.md) for board routes and pinned dependencies.
2. **Prepare trust and sources.** Use private enrollment and the
   [shared registry](../../registry/README.md), with a compatible BLE mesh.
   Configure a matching LoRa transmitter to enable the fallback receiver.
   Keep actual device bindings, credentials and enrolled images in ignored files.
3. **Build and preview offline.** Run the native builder and host UI tests in
   the build guide. The builder opens no hardware; its output is not installation
   authorization or a timing qualification.
4. **Install on the verified device.** Use compatible recovery, the original
   bounded application drain, app-only write bounds and full application readback.
   The [USB ROM tool](../../docs/twatch/build.md#usb-rom-entry) avoids button
   handling when the application is responsive; physical recovery remains available.
5. **Choose a face.** Swipe horizontally, or open **MODE → Watch faces**.
   Use **MODE → Settings** for brightness and sleep time.

## Screens and controls

| Launcher | Settings |
| :---: | :---: |
| [<img src="../../docs/twatch/images/launcher.png" width="164" alt="Watch launcher with Watch faces and Settings entries">](../../docs/twatch/images/launcher.png) | [<img src="../../docs/twatch/images/settings.png" width="164" alt="Display settings for brightness and sleep timeout">](../../docs/twatch/images/settings.png) |

| Control | Action |
| --- | --- |
| Swipe left or right on a face | Switch between Digital and Instrument, and remember the selection. |
| Tap MODE or press the user button | Open the launcher. |
| Watch faces | Select Digital or Instrument. |
| Settings | Cycle brightness or the 90/120/180-second timeout. |
| First touch or button after display sleep | Wake only; a second gesture operates the interface. |

Swipes resolve on release, so crossing MODE does not accidentally open it.
The complete first gesture after sleep is consumed. Invalid saved settings use
defaults; older short timeouts migrate to 90 seconds. Storage errors do not erase
whole-device NVS.

## Read the faces

Digital shows time, date, selected source, **Sync RMS**, **PLL** state and the
sampled **P99 sub-1 µs hold duration**. Unknown or expired evidence shows `--`.
Instrument presents those measurements mechanically:

| Indication | Meaning |
| --- | --- |
| Source aperture | **BLE** includes BLE-derived RC1/RC2; **LOR** is LoRa; **RC9** remains distinct. **HOLD** marks retained time and `---` means unavailable. |
| PLL phase well | Full means current relative lock, half means hold, dark means acquisition/unavailable. **NO TIME** is separately indicated. |
| RX jewel | LoRa receiver powered; it does not mean a fresh packet or selected timing source. |
| Sync RMS | Linear **0 ns–10 µs** dial. **OVER** marks values above 10 µs. |
| P99 hold | Sampled duration on a **1–60 second** scale, with inclusive endpoint limits. Lower-bound-only evidence is unavailable on this analog dial. |
| Reserve | Battery level from E to F, not a prediction of remaining runtime. |

Red parked subdial hands mean unavailable. RMS is internal prediction quality;
PLL lock is the shared model's relative-lock state. P99 is an empirical sampled
hold statistic, distinct from a model-estimated horizon or calibrated UTC accuracy.

## Timing, power and limits

BLE is preferred; passive LoRa stays warm when configured. Returning from live
LoRa uses N30's qualified **30-second BLE-return dwell**. Each source retains its
own authentication, freshness and timing permissions. See the
[N30 recovery guide](../../docs/n30/dual-timing-state-machines.md) for backend rules.

A separate display-only clock bridges missing snapshots for up to **24 hours**
from its last valid anchor. It retains the timezone, shows HOLD and withdraws
precision indicators. This does not extend a source lease, synchronization
permission or the measured P99 statistic. It clears on reboot.

Display sleep saves panel power while BLE, LoRa and USB remain available.
GNSS shadow PPS is deferred. Battery endurance, sleep current and uptime targets
remain in the [power and qualification plan](../../docs/twatch/power.md).

This target is under development. Host tests and native screenshots establish
software properties; finite hardware checks do not establish continuous
availability, calibrated UTC/output accuracy, qualified failover or battery life.
No W12/N30 calibration or hardware acceptance transfers to the watch, and no
enrolled binary is distributed. [Source provenance](imports.json) and
[dependency notices](NOTICE.md) describe the reused components.

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
Preserved dependency notices apply.
