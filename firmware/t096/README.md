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
After a previously qualified GM BLE/local source fails, its old LoRa timing
watch is cleared while LoRa receive observations and replay protection remain
active. The GM precision screen stays on native BLE/GM timing. RC mode keeps
its independent LoRa fallback and HOLD display.

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
| Display behavior | Front digital seconds, actual local PPS and clock-loss indications receive priority; other regions update opportunistically. Whole-page frame rate varies with timing work. Eligible screensaver starts after **5 minutes** idle at **50% brightness**. |
| BLE timing | Authenticated **G1/RC7** precision reception and relay. Current coarse TZ wire uses **GM7 revision 4, JOIN revision 5, RC control/capability revision 8**. |
| GM coordination | Broadcast ANN/STATUS/ROLE, persistent G1 following without GM admission, local HOT readiness and independent GNSS qualification. Client GATT admission remains required. |
| LoRa timing | **Passive wire3** fallback with one designated transmitter, exporting qualified local GM time or authenticated locked BLE time. |
| USB | Hardware-captured USB frame boundaries; RC exports BLE time, DC exports local GNSS time to the macOS client. |
| Local holdover | BLE-only guarded rate consensus, paired frozen shadow clocks and a120s local watch estimate; [policy and verification](../../docs/t096/holdover.md). |
| Time zone | GM uses its GNSS location and pinned IANA offset calendars; manual-zone/fixed-offset overrides. Live mesh metadata remains leased. The display retains its learned zone for the current boot, with pinned DST calendars. |
| Development | Zephyr-based native builds with pinned public dependencies; private device binding and GM/RC enrollment. No enrolled binary is distributed. |

W12, N30 and CoreS3-SE consume the current coarse TZ profile; StickS3 retains
its own selected profile. Check each target's guide before pairing images.
Precision payload names alone do not establish coarse-wire compatibility.
[Wire contract](../../protocol/time-zone-v1/README.md) ·
[W12/N30 TZ behavior](../../docs/esp32-time-zones.md) ·
[Automatic time zones and DST](../../docs/t096/auto-time-zone-plan.md).

Clock validity and button/menu handling continue independently of optional rendering.
The PPS lamp follows the captured local pulse's250ms display window; it is not a
synthetic BLE beat. On-demand USB diagnostics distinguish critical-region SPI
completion, event misses and rendered timestamps from a completed whole page or
analog face. SPI retirement does not establish optical scan time or independent
UTC accuracy.

## Passive LoRa fallback

The [current LoRa design](../../docs/t096/lora-protocol.md) defines passive wire3,
separate from historical RC8. Select the slow 30/120/300 s variant and one exact
transmitter factory at build time. Listeners request RX at boot in both GM and
RC profiles; GM participation does not suppress passive RX. The designated
GM-profile device requests TX and may export qualified own-GNSS time or fresh
authenticated qualified BLE time from another GM. Becoming ACTIVE does not enable
LoRa TX on another device. There is no automatic backup emitter.

The second (RF) screen shows both BLE and available LoRa time in GM and RC,
including while BLE is the primary source. The active transmitter shows its TX
state instead of a receiver clock. During acquisition the display explains
whether it is waiting for META, waiting for a pair, building its fit, or missing
pairs. RX lamps pulse for decoded timing or unknown payloads, rather than raw
carrier activity.

BLE acquisition takes display priority over unqualified LoRa HOLD/EST. The RF
page compares the independent clocks at one hardware capture: DELTA ns is BLE
minus LoRa, and PPB compares their delivered rates. Fresh provisional values are
marked EST; an unmeasured rate or expired clock shows `--`. These on-demand
display comparisons grant no synchronization, relay or source authority.

The slow profile sends precision every30s, discovery every120s and full META
every300s. Matched schedule-3 receivers are required; the7s SYNC offset and
packet-level source/capture guards remain mandatory. GNSS and BLE timing retain priority. Internal precision/META retries
retain owed work after proven closure; clients send no ACK or retransmission.
See the [build guide](../../docs/t096/build.md#optional-lora-configuration) for
selection and the [soak guide](../../docs/t096/lora-soak.md) for measured checks.

The matched fixed500 setup selects schedule8: a single922.4MHz/SF11/BW500/CR4/8
PHY,30s precision pairs and360s META with no DISC. Missed META remains due on
the next eligible precision slot. Nominal guarded airtime is77.467040s/h under
a90s/h engineering cap. RX stays continuous. An available
LoRa/HOLD watch in RC remains selected until BLE has been fresh and qualified on the
same authenticated scope for30 continuous seconds. Loss, expiry, refusal or a
context change restarts that dwell while fallback is selected. Once BLE owns
the watch, its common clock validity governs fallback; a separate LoRa-export
quality refusal does not withdraw that BLE clock. Neither fallback nor a held watch grants
BLE relay, USB timing or GM authority. See the build guide for its separate
configuration, frozen endpoint calibration and watchdog/rearm limits.

In GM mode, loss of a previously qualified BLE or local-GM clock discards the
LoRa timing and display maps. Decoded reception, authenticated META/TZ and
replay protection remain available. Fresh normal timing or an explicit RC
switch releases this restriction. Existing GM cold-start behavior before the
first qualified normal clock is unchanged. The GM precision page always shows
native BLE/GNSS status; the separate RF page continues to observe both radios.
Passive LoRa reception currently does not inhibit GM7 elections or provide
protection against every competing active GM.

Runtime state is RAM-only: boot resets airtime, cooldown/session state, fits,
faults and replay history. Profile/source changes within a boot retain airtime.
Historical LoRa ledger bytes remain unused and untouched. Fault-only evidence can
append to a separate16-slot diagnostic journal before radio startup, with no
erase or runtime writes. Full storage retains RAM and reports failure; see
[durable fault readout and limits](../../docs/t096/diagnostics.md#fault-only-reset-retention-and-durable-readout). The 108 s per
rolling-hour engineering cap has no cross-boot enforcement and establishes no
FCC compliance. The [design](../../docs/t096/lora-protocol.md#range-and-fcc-compliance-considerations)
records RF/antenna considerations and future slow-profile range goals; field
coverage, power and emissions remain unmeasured.

LoRa can drive the watch when BLE is unavailable. The fixed500 receiver shows
digital time after its first authenticated physical pair, with **ERR UNKNOWN**
until a second pair measures the previous clock's prediction error. **PRED RMS**
is the RMS of the latest eight known pre-update errors; it is retained with its
actual age through loss. It is a relative packet-clock measurement, not an
independent UTC bound. Source identity, RX strength and the **30 s** badge use
their original observations. UTC remains the internal coordinate. The qualified
fit still requires at least180s of physical observations; early display does not
wait for it or grant its permissions. Other passive profiles retain **FIT RMS**.
LoRa grants no GM service/election, HOT, BLE relay or USB timing authority.
Passive wire3 has no RTT/ToF exchange.

Console `pair_prediction` separately reports the previous delivered clock's
signed error at the next accepted physical pair, with explicit unknown and
clipped states. Use the retained physical interval and context when assessing
prediction or HOLD performance.

Fixed500 acquisition shows amber **ACQ / EST** in the analog area while the
digital clock advances. Missing pairs show **HOLD / EST** and pair/error ages;
expired META shows **META STALE**. The digital watch and error history survive
long RF gaps while local capture remains continuous. Learned authenticated META
and TZ stay in boot RAM. Same-context packets can update this human observer
after the META lease expires; they cannot renew live grants, shadow PPS or
export authority. A same-context META refresh preserves its history. OFF,
context/capture changes, authenticated TIME_INVALID and faults fence timing;
fresh authenticated admission is required to resume. Descriptive cache alone
cannot readmit it. Acquisition/held display blocks the screensaver and has
unknown uncertainty. Other slow profiles retain their original frozen display
reference and age limits.

Authenticated selected TZ can teach a boot-local display cache before UTC.
Valid changed metadata replaces it; unknown metadata and timing loss do not
erase it. Known geographic zones follow pinned calendars at displayed UTC;
calendar failure hides conversion while retaining identity. Reboot clears the
cache. It grants no timing authority.

For one bounded lab META/pair, use the boot-bound
[`lora-utc` diagnostic](../../docs/t096/lora-utc-diagnostic.md). Its explicit
per-job cap override applies only to that request, never to ordinary
qualification. It does not bypass source/capture, channel or physical RF guards
or replace180 s training. The finite fast variant and historical RC8 controls
are described in the build guide; check the selected image before using them.

Presentation source/time and hold histories use the main loop's current owned
snapshots independently of display jobs. The status reply includes
`ui_logic:[age_ms,flags]`: bits1 time valid,2 BLE permission current,4 LoRa primary,
8 visual loss grace,16 snapshot fresh,32 local GM,64 RC local HOLD and128 maintained.
These are presentation states, not precision qualification. `ui_input[0]` is the
last completed whole-page model timestamp; it does not measure current logic or
critical-region freshness.

Source labels are descriptive caches keyed to actual factory identities. Within
this display's boot, names survive the selected source's reboot/epoch changes,
short timing gaps and deferred frames; fresh trusted metadata replaces them.
A different selected factory gets its own alias/name. Labels never grant time or
extend an owner deadline. `ui_close_result` reports actual UI release completion
independently of the last draw error in `ui[5]`, which may retain a recovered error.

The on-demand USB `clock` reply exposes `clock.ui_critical` and `clock.shadow_pps`.
`ui_critical` reports eligible/committed/missed/canceled/unknown event populations,
pending work, late counters, source fences and per-region render/submit/retire
stamps. Compare counter deltas only within a compatible source/page population;
use actual captured PPS edges, not query samples, as the input denominator.
A partial tile never acknowledges a whole page or analog face.
`shadow_pps` reports independent lab session/window/cursor and summary evidence;
the status reply also retains `shadow_ui`, and raw `shadow` readout remains
available under its existing eligibility and RAM-retention rules. Late reference
association can prevent reconstruction of the exact UI accepted set, even with
cursor bounds. These queries add no periodic stream, timing authority or
independent UTC/optical accuracy claim.

## Button controls

| Action | Result |
| --- | --- |
| Short press outside the menu | Next page. A short press from the screensaver only wakes the display. |
| Hold **2.0 s** on any ordinary page | Open the menu. Release before selecting an item. |
| Hold **1.5 s** on the RF page | Show RF help; release to keep it visible, then tap to return. Continuing to **2.0 s** opens the menu. |
| Short press in the menu | Next enabled item; wrap after Exit. |
| Hold **2.0 s** in the menu | Select the highlighted item once. A continued hold cannot select again. |
| Select **Exit**, or leave the button alone for **5 s** | Close the menu and return to the invoking page. Each press restarts the idle interval; a held button cannot time out. |

Except for the RF help gesture, releases before **2.0 s** are short presses; the hold duration uses the
observed button edges so release debounce cannot turn it into a selection.

The ordinary GM/RC menu contains **GM Mode / RC Mode**, **Power Off**, **BLE RX
Off / On**, **LoRa RX Off / On**, and **Exit**. Rows name the next action; the
header shows the current mode. Changes keep the menu open and show a brief
result. Power Off requires its own selection hold and uses orderly shutdown.
The separate DC/PC images retain their own controls.

The factory-bound LoRa transmitter has **LoRa TX Off / On** in the LoRa row
instead of RX. Its TX row is unavailable in RC. Entering GM re-enables its TX
request; a manual OFF otherwise remains in effect for that GM session.

BLE RX OFF drains and parks the BLE owner in either GM or RC mode. It does
not restart either profile, elect a GM, or restart the display, GNSS/capture or
LoRa owners. BLE and local-GNSS timing are unavailable to the watch while OFF;
only a fresh independent LoRa receive source can supply its time. Intentional
OFF suppresses automatic BLE recovery and its no-sync watchdog. Core and LoRa
fault protection remain active. Re-enabling BLE uses a fresh locally discovered,
authenticated context. Profile selection while OFF does not start BLE.

Radio requests are volatile; an ON request does not prove RF activity or valid
time. Source, cap or hardware gates may still inhibit LoRa.

The precision page separates stream RMS, local PPS continuity and ACTIVE/HOT
duration. Shadow-PPS compares the selected BLE or LoRa clock with local PPS
when eligible. LoRa records expose raw phase and any explicitly configured
endpoint-delay correction; passive wire3 has no RTT exchange. PPS never trains the LoRa clock.
The independent lab remains available in ordinary GM/RC images when following
an eligible independent source. Serving-GM self-comparison is excluded. UI and
manual USB requests are independent, and neither feeds timing discipline.
Completed raw USB witness retention remains16 RAM records; readers retain invalid,
missing and overwritten counts. Integer mean/RMS/max keep their existing rounding,
and percentiles keep the original bounded histogram and tail semantics. No
runtime PPS flash writes are added.
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
