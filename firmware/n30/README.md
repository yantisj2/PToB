# N30 · BLE clock with LoRa fallback

PToB firmware for the **Meshnology N30 / Heltec WiFi LoRa32 V3**: receive
mesh time, view a portrait clock and compare BLE with an independent passive
LoRa timing input. N30 supports **RC (receiver/relay clock)** only. It has no
GNSS input, and Wi-Fi is disabled.

[Build and set up](../../docs/n30/build.md) ·
[Group setup](../../docs/GETTING_STARTED.md) ·
[RC9 fallback](../../docs/n30/rc9.md) · [Changelog](CHANGELOG.md)

| Clock | BLE + LoRa | Precision | LoRa receiver |
| :---: | :---: | :---: | :---: |
| [<img src="../../docs/images/n30-clock-preview.png" width="128" alt="N30 portrait clock dashboard">](../../docs/images/n30-clock-preview.png) | [<img src="../../docs/images/n30-rf-preview.png" width="128" alt="Independent BLE and LoRa clock readouts">](../../docs/images/n30-rf-preview.png) | [<img src="../../docs/images/n30-precision-preview.png" width="128" alt="N30 precision preview with internal causal RMS, relative drift and counter details">](../../docs/images/n30-precision-preview.png) | [<img src="../../docs/images/n30-lora-preview.png" width="128" alt="N30 LoRa receive status">](../../docs/images/n30-lora-preview.png) |

*Production-renderer previews, not hardware photographs. The precision image's
RMS is a recorded internal prediction statistic; other values are illustrative.
These values are not specifications or independent output accuracy.
[Image provenance](../../docs/images/README.md#n30-precision-preview).*

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Hardware | ESP32-S3, **8 MiB flash** build configuration, **SX1262 LoRa** radio. Fixed board routes; no GNSS profile. |
| Display | **64 × 128** portrait monochrome OLED with clockwise/counterclockwise mount selection, digital/analog clock and moving savers. |
| BLE receiver | Authenticated **G1/RC7**. **BLE100** is standard; **BLE50** is client-only. Optional BLE100 relay uses estimated TX timing with unknown RF/UTC bounds. |
| Fine clock | Optional continuous **80 MHz counter**, **12.5 ns ticks**, delivered on a **50 ns grid**. Requires N30-specific capture routing and calibration; unconfigured builds retain the **20 MHz** backend. |
| LoRa | Passive **wire 3 / schedule 8**, fixed **922.4 MHz**, SF11/BW500 kHz/CR4/8 with SX1262 boosted RX gain, matching an independently configured T096 transmitter. Independent **80 MHz MCPWM group0** HeaderValid capture/model, with the original GPTimer mapping retained as an admission guard. |
| Functional fallback | Distinct **RC9** direct-client UTC reception with unknown transport/precision bounds; it cannot relay further or train the BLE fitter. |
| Time zone | Clock and savers retain the latest learned authenticated BLE/LoRa time-zone offset in RAM until reboot. New metadata updates it. Timing diagnostics remain UTC. |
| Power and console | GPIO0 page/menu controls and bounded deep-sleep request. UART diagnostic console, not a T096 USB timing source. |

## Get started

1. **Verify the board and identity.** Confirm the factory MAC, board revision,
   display wiring and mounted rotation. Use the [onboarding guide](../../docs/n30/build.md)
   to bind the device to the shared registry; never substitute an alias or
   advertised BLE address for factory identity.
2. **Prepare a compatible source.** Use the [group setup](../../docs/GETTING_STARTED.md)
   with a T096 GM or compatible relay. Match the current
   [coarse TZ wire](../../docs/esp32-time-zones.md). Keep trust enrollment separate
   from runtime discovery; a context change needs fresh authenticated admission.
3. **Build and preview offline.** Follow the [build guide](../../docs/n30/build.md)
   for public dependencies, board configuration, both portrait rotations and
   tests. Enable estimated relay or fine capture only through their explicit
   profiles. Keep enrollment, device bindings and images in ignored local files.
4. **Use the exact-device installation path.** Default builds are offline
   preparation. Commissioning needs verified recovery, physical layout checks,
   bounded owner drain, app-only write bounds and actual readback. Then check
   the selected parent, source freshness and relative lock before interpreting RMS.
5. **Add LoRa if needed.** Configure the matching T096 transmitter and receive
   profile using the build guide. N30 listens passively; it does not supply the
   LoRa source. Validate fallback separately from normal BLE reception.

## How the timing inputs work

Settled BLE lock has display priority at startup. Fresh direct LoRa supplies
the **LOR** fallback, ahead of RC9. Once LoRa is selected, BLE must remain fresh,
authorized and locked for **30 seconds of qualified time** in the same admitted
scope before it takes over. Permission-only gaps below1.5s pause that evidence.
Longer gaps, quality/context changes and hard fences reset the dwell. An expired or
unqualified LoRa HOLD yields immediately to fresh authorized settled BLE.
The return dwell observes actual source quality at main-owner cadence,
independently of OLED/publication timing. The selected input supplies the displayed clock as well as its label. Each input keeps its own clock: LoRa and RC9 never train
the BLE model or grant downstream timing authority. The dual-radio page compares
clocks only when root, boot and source scope match.

LoRa **AUTO** and **KEEP ON** listen continuously, including during healthy BLE
and a BLE-owner fault,
so the authenticated 300-second metadata context and independent timing fit stay
warm for fallback. **KEEP OFF** stops reception. Overrides reset to AUTO at boot. Bounded passive discovery and fresh admission allow normal BLE to return
during an RC9 lease; root hints or failed admission never renew fallback expiry.

BLE acquisition has its own recovery supervision, independently of LoRa.
After15s without a new physically matched corrected BLE pair, a recently seen
eligible parent can trigger a BLE-only rebind. Existing admission/create work
finishes first; the original2s owner drain and fresh authentication still apply.
Advisory retry history is cleared after successful closure. Ordinary RF absence,
quality dips with continuing physical progress, explicit OFF and actual faults
do not trigger this path. Operator parent/profile choices remain in force.
The GM fitting trial begins only after usable authenticated admission. A failed
first exchange cannot reserve a minute and block fresh alternative parents.
The chooser also honors the same retry cooldown as admission, so a stronger
peer with a no-time reply cannot repeatedly block a weaker usable alternative.
Authenticated direct JOIN may bootstrap with up to200ms of declared coarse
uncertainty, within the existing250ms exchange limit. The complete causal RTT,
source bound and drift margin still shorten the source grant. This coarse
estimate supplies replay context only; fresh physical corrections and the
existing qualification gates are required for BLE precision lock.

After direct-GM loss, a trained local estimate may remain labeled **HOLD**
through a successor's coarse JOIN. Reuse ends **30 s** after the original physical
observation; only a fresh authenticated physical correction resumes steering.
Source permission, fit samples and lock quality are acquired afresh. This
continuity mechanism does not establish qualified failover precision.

The [dual-timing state-machine guide](../../docs/n30/dual-timing-state-machines.md)
describes source/owner boundaries, offline sequence checks and retained
`dual read` diagnostics for source-selection and parent transitions.
Commissioning candidates require a passing receipt bound to their source files.

Learned parents, models and retry history live in RAM and start empty on boot.
A watchdog can restore an explicit operator pin; it cannot restore learned
clock authority. Enrollment, registry and calibration remain separate inputs.

## Screens and controls

A press released before **2.0 s** cycles **Clock → BLE + LoRa → Precision →
LoRa → Clock**. Hold **2.0 s** on any page to open the watch menu, then release.
Acquisition continues while the startup splash is displayed.

Inside the menu, a short press selects the next enabled row; a separate
**2.0 s** hold applies it once. The opening hold cannot apply a row. Changes
keep the menu open. **Exit**, or **5 s** without input, returns to the invoking
page; a held button cannot time out. A short screensaver press wakes, and a
continued 2.0 s hold opens the menu.

| Menu row | Action |
| --- | --- |
| UPSTR AUTO / UPSTR RLY | Select the opposite upstream parent policy: automatic or relay-only reception. N30 remains RC-only; it has no GM mode. |
| POWER OFF | Request bounded shutdown. |
| BLE RX OFF / ON | Park BLE reception, or request a fresh authenticated client activation. Continuous AUTO reception remains available; manual LoRa ON/OFF choices remain unchanged. |
| LoRa RX OFF / ON | Set a volatile KEEP OFF / KEEP ON override. The row is omitted without an enrolled transmitter. AUTO remains available through the bound `lora auto` console command and is restored at boot. |
| EXIT | Return to the invoking page. |

While BLE is parked, only fresh LoRa can supply time. BLE/RC9 clocks and cached
BLE pixels are withdrawn; missing LoRa time does not trigger a no-time restart.
BLE ON requires fresh admission; a selected LoRa clock retains the 30-second
qualified BLE recovery dwell.
Actual LoRa faults, all-OFF, maintenance and shutdown retain their existing
fences. A BLE controller fault remains visible and keeps the original fault
watchdog; it cannot revoke an otherwise healthy independent LoRa receiver.

Rows describe the requested next action. **REQUESTED** means the owner accepted
the request; **REFUSED**, **SWITCHING**, radio wait and fault states remain
separate from active reception or valid time. UPSTR is skipped while BLE is OFF.

Release after selecting Power Off. Deep sleep requires the original **2 s**
radio/capture drain and display-worker stop; a new button press reboots the app.
A failed drain remains a fault. Sleep current, battery lifetime and physical
wake behavior need hardware validation.

The clock's signal bars describe fresh authenticated parent RSSI, independently
of lock, and expire after **2 s** without a packet. Precision follows the actual
**CLOCK** input. BLE/RC1/RC2 show their root/path, **BLE RMS**, relative drift
in ppb, counter/tick size and **BLE LOCK** duration. Relative BLE drift is a
diagnostic and does not steer time.

For **LOR**, Precision shows the LoRa **TX** emitter and **ROOT**, its independent
80 MHz MCPWM group0 counter (**12.5 ns** hardware ticks), and **E**, the retained
software admission enclosure rounded up to microseconds. The independent
estimator uses normalized16MHz coordinates (**62.5 ns** quantization); hardware
tick size is not delivered-clock accuracy. E is not a calibrated RF error bound.
**PRED RMS** records actual causal predictions before each accepted physical
pair steers the model. A first pair supplies estimated digital UTC with unknown
error; pair two supplies the first measured prediction error. The last eight
available residuals form the visible RMS. Console totals retain the complete
population, maximum, count above1us, actual model age and prediction mode.
Neither an exact two-point fit nor internal covariance is an uncertainty bound.

The original segmented digital clock remains visible during acquisition and
poor RF. Acquisition, pair count/span, META status and error ages appear inside
the analog-clock area. An independently acquiring LoRa observer does not replace
a usable primary BLE clock. Qualified LoRa retains priority over functional RC9;
unqualified observer time does not. The original180s qualification, capture,
source and physical120s grant limits remain distinct from human presentation.

Authenticated META source/context descriptions and TZ remain in boot RAM after
live META expires. Same-context authenticated SYNC/FOLLOW pairs can continue an
explicitly unqualified observer and update its estimated time and real causal
error; cached metadata cannot admit a new context or renew a source lease.
Fresh same-context META preserves that observer history. One or two missed pairs
produce aged **HOLD** with continuous digital UTC and visibly aged RMS. Beyond
300s without a pair, human HOLD freezes the derivative at300s and drops residual
acceleration. Its original pair/error ages keep increasing. This approximation
has no precision lock, PPS or downstream authority. Counter faults, source
withdrawal, radio faults and OFF fence observer time; fresh META is required for
readmission. Descriptive cache/TZ survive those fences until reboot.

Healthy active LoRa acquisition does not trigger a no-time restart while waiting
for first META, including a missed300s META. Once useful authenticated boot
information has been learned, ordinary RF absence likewise retains that state. The no-time restart interval is **12 minutes** when armed. Actual driver, counter,
transport, cleanup and core-fault watchdogs retain their original120s deadline.

The N30-only LoRa estimator uses phase, rate and two damped acceleration modes
(30s and120s), driven solely by physical LoRa captures. It remains a development
candidate. Raw shared fit, delivered next-pair history, analytical frozen60s
forecasts and actual finite60s HOLD trials retain separate diagnostics. The
analytical statistic is not an independently measured output or UTC error.
**UTC/RF UNKNOWN** preserves those unknown bounds.

## Precision, evidence and development

**Causal RMS is internal prediction quality.** It differs from downstream
relative PPS error, counter resolution and calibrated RF/UTC accuracy. The
sub-100 ns downstream goal remains open. LoRa uses an **unverified 99.328 ms
engineering symbol-delay model**, with unknown board/RF endpoint bias; internal
fit RMS does not measure UTC accuracy. Calibration constants do not transfer
between boards without independent checks.

The [timing research draft](../../papers/ptob-esp32s3-timing-preprint.pdf) and
[selected evidence](../../evidence/s3/2026-10-22-fine/README.md) retain finite
measurements, populations, failures and tails. The [changelog](CHANGELOG.md)
records dated results and firmware changes. Host tests, native builds and
previews establish software properties, not hardware acceptance.

Use the [build guide](../../docs/n30/build.md) for fine-receiver contracts,
diagnostics and update checks. [Source provenance](../../docs/n30/source-provenance.json)
and [notices](NOTICE.md) identify the selected W12/T096 dependencies.
No enrolled binary is distributed. Independent UTC/output accuracy, RF endpoint
delay, physical failover and continuous availability remain unqualified.

Copyright (c) 2026 Jonathan Yantis. Original documentation: [MIT](../../LICENSE).
Preserved dependency notices apply.
