# Linux USB/SOF association: user-space uncertainty ledger

Copyright (c) 2026 Jonathan Yantis. MIT.

Research and prototype scope: 2 November 2026. The
[shadow observer](../../gateways/pi5/usb/README.md) evaluates T096 RC USB
as an alternative input while the existing M-HAT/GPIO path continues to own
discipline. No kernel patch, custom kernel, module load/change, firmware change
or clock-source promotion is part of this work. This ledger distinguishes API
semantics from measurements and unknown physical terms.

## Available host interfaces

Apple's `frameNumberWithTime:` returns a controller frame number without its
microframe and an associated system time. The existing macOS helper additionally
uses `referenceMicroframeWithTime:error:` and explicit causal UFA1 markers.
Those local implementation checks are part of its association, not a portable
Linux capability. See [Apple's frame API](https://developer.apple.com/documentation/iousbhost/iousbhostobject/framenumberwithtime%3A)
and the [repository bridge](../../clients/macos/USBFrameBridge.swift).

Linux's [usbfs UAPI](https://raw.githubusercontent.com/torvalds/linux/v6.18/include/uapi/linux/usbdevice_fs.h)
provides bounded control transfers, interface claims and asynchronous URBs.
It has no generic frame-number-plus-host-clock ioctl. `start_frame` in an ISO
URB is not a timestamp of an EP0 response. The
[libusb API](https://libusb.sourceforge.io/api-1.0/modules.html) does not supply
an Apple-equivalent frame/clock association. `SYNCH_FRAME` is a USB standard
device request identifier, not such a host API; see
[libusb's request definitions](https://libusb.sourceforge.io/api-1.0/group__libusb__misc.html).

[usbmon](https://docs.kernel.org/usb/usbmon.html) observes HCD requests and
callbacks rather than physical bus transactions. Its ISO-only start-frame
field does not label EP0 source requests with SOF time. In the
[Linux 6.18 implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/usb/mon/mon_bin.c),
binary timestamps use realtime and microsecond fields. These could help measure
software transport delays, with a new observer population, but cannot silently
become hardware SOF timestamps. No usbmon module is loaded by this prototype.

The stock xHCI driver internally obtains a full frame by reading MFINDEX and
shifting by three; it also identifies the integrated transaction translator.
See [`xhci_get_frame` and root-hub setup](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/usb/host/xhci.c).
The [xHCI specification, §5.5.1](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
defines MFINDEX as a 14-bit read-only counter advancing each 125 µs, with bits
13:3 denoting a 1 ms frame. This defines a counter, not its timestamp in Linux
RAW time or its physical phase at a downstream full-speed device.
Section4.14.2 and Figure4-21 map USB2 SOF FrameNumber to MFINDEX[13:3]. The
prototype reports a conditional spec-mapped RAW shadow using the preceding
host-transition fit; implementation conformance and physical phase remain
unqualified. The spec's beginning/end wording does not replace event validation.

The existing [Raspberry Pi xHCI debugfs code](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.18.y/drivers/usb/host/xhci-debugfs.c)
creates a read-only `reg-runtime` register set, with MFINDEX first, at the
controller's runtime-register offset. It reads eight registers, not just one.
The [debugfs regset implementation](https://codebrowser.dev/linux/linux/fs/debugfs/file.c.html)
performs `readl` for each entry and runtime-PM get/put around the register set.
The prototype checks the controller is already active, opens only this named
stock view, rewinds before every read and retains outer RAW read brackets.
There is no `/dev/mem`, BAR mapping, live register write or driver bypass.
[Debugfs is not a stable userspace ABI](https://docs.kernel.org/6.1/filesystems/debugfs.html);
format, kernel and controller identity must accompany each population.

Raspberry Pi's [RP1 peripheral specification, chapters2 and5](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)
identifies two Synopsys dwc_usb3 v3.30b xHCI1.2 controllers and a50MHz crystal
reference with dedicated USB PHY PLLs. It describes independently provisioned
downstream USB2 bus instances. That architecture supports the direct-port
selection, but does not give a calibrated RAW/USB frequency ratio or a
submicrosecond cross-clock latch. The
[DWC3 Linux host glue](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.18.y/drivers/usb/dwc3/host.c)
creates the xhci-hcd child and memory/IRQ resources; it does not expose an
additional userspace frame timestamp. The
[DWC3 debugfs dump](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/usb/dwc3/debugfs.c)
is a larger register dump, not an atomic frame/RAW association. Reading fewer
text bytes from a seq_file does not make its register-set show callback read
only MFINDEX. Direct MMIO mapping or added kernel probes are not substituted
for a validated stock timestamp API in this experiment.

A separately labeled syscall experiment can use `--host-read-method pread`.
Linux's [`seq_file` implementation](https://codebrowser.dev/linux/linux/fs/seq_file.c.html)
supports positional reads and resets its iterator at offset zero; this permits
one `pread(fd,1024,0)` instead of separate `lseek` and `read` syscalls. Live
freshness must still be demonstrated by advancing MFINDEX observations and
counter/frame coherence. Both methods retain complete RAW brackets and the
same read budgets. This optimization still executes the eight-register show
callback and runtime-PM references; it does not provide an atomic timestamp or
justify assigning the first register a narrower unmeasured internal timestamp.

The separate `native-pread` mode puts RAW reads, positional reads, strict frame
parsing and the bounded burst loop in an ordinary C user-space helper. It can
remove Python allocation/regex/loop gaps and their CPU cost; it cannot remove
the stock callback's eight MMIO reads, runtime-PM/seq_file/driver overhead or
unknown timestamp location within that syscall. The optional helper is built
with an existing compiler in a private temporary directory and removed at
exit. Acquisition boundaries, preparation/cleanup, compiler child CPU and
whole observer self/child costs are explicit. No kernel component is added.

Linux also exposes class-specific
[UVC metadata](https://docs.kernel.org/5.16/userspace-api/media/v4l/pixfmt-meta-uvc.html)
with payload-reception time and a nearby USB frame number. That interface
belongs to camera payloads and their UVC driver; it is not a generic EP0 frame
timestamp API for this vendor timing interface. Adding another device or
changing firmware class is not part of this experiment.

## Stock event and tracing interfaces

Additional Linux6.18 source checks close several apparent alternatives:

- [`usbdevfs_urb.start_frame`](https://github.com/torvalds/linux/blob/v6.18/include/uapi/linux/usbdevice_fs.h)
  is supplied on submission. In the reviewed
  [`devio.c`](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/core/devio.c),
  native and compat completion-copy routines return status, lengths and ISO
  packet results, without copying the kernel URB's updated `start_frame` back.
  The presence of that field therefore does not establish an observed frame
  label for an EP0 completion or any associated host-clock value.
- The [xHCI specification,§6.4.2.8](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)
  defines a MFINDEX wrap event with type/cycle/completion fields and no hardware
  timestamp. An event-ring software observation adds interrupt/DMA/service
  delay; its timestamp cannot be treated as the wrap instant. Enabling wrap
  events changes the controller command register and is outside this read-only
  experiment. The reviewed
  [event handler](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci-ring.c)
  does not provide a dedicated wrap-time cross-clock association.
- The [xHCI sideband API](https://github.com/torvalds/linux/blob/v6.18/include/linux/usb/xhci-sideband.h)
  serves kernel clients, exposing transfer/event buffers and endpoint/interrupter
  operations. Its
  [implementation](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci-sideband.c)
  changes endpoint/ring ownership and offload state; it is not a passive
  userspace SOF/RAW timestamp API. No sideband operation is performed.

These are pinned upstream source semantics, not proof that another Pi binary
has identical implementations. None supplies the missing physical SOF-to-RAW
latch. The RP1 USB chapter documents controller version, ports and bus instances,
but no such latch or bounded SOF/CPU phase calibration. Absence from these
reviewed APIs is a feasibility limit, not proof that no undocumented facility
exists. The supported observer retains complete read brackets and unknown
controller/wire/capture bias.

The xHCI specification does define an optional
[Intel Time Stamp Correlation Capability,§7.10](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf).
Its Get Extended Property command can return a synchronous hardware-system-time
and USB-interval/delta snapshot. That is a real hardware correlation mechanism
in the specification, not a generic libusb call. Support on the RP1 controller,
the meaning of its system counter and an existing stock userspace access path
have not been demonstrated. The reviewed extended-capability and usbfs paths
expose no such passive userspace command. A debugfs directory listing covers
only selected capability classes, so missing timestamp files cannot prove
hardware absence. Submitting command-ring work or mapping driver-owned memory
is outside this observer; no such command or capability probe is performed.

The Linux6.18 [xHCI trace schema](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci-trace.h)
records TRB fields, ring/DMA identity or URB fields. It does not export an
atomic MFINDEX/host-clock pair. In the
[event-ring implementation](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci-ring.c),
`xhci_handle_event` is traced when software processes a DMA event TRB, before
dispatch. `xhci_handle_transfer` traces the endpoint TRB; URB giveback is traced
before calling the USB core. These points follow controller event generation,
DMA visibility, interrupt delivery/moderation, locking and event-ring handling.
Their CPU timestamps cannot be substituted for the physical transfer or SOF.
No calibrated latency bound for those stages has been demonstrated here.
The header recognizes MFINDEX-wrap TRB type39, but this handler provides no
per-SOF timestamp path. A type definition is not a supported frame/time API;
wrap-event generation or controller command changes are outside this scope.

The [HCD core](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/core/hcd.c)
provides a kernel `usb_hcd_get_frame_number()` call through the HCD operation.
The [xHCI operation](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/host/xhci.c)
returns a register read shifted by three; it does not return a host timestamp.
USB-monitor hooks are at URB submission and software completion. Its
[binary record implementation](https://github.com/torvalds/linux/blob/v6.18/drivers/usb/mon/mon_bin.c)
uses realtime seconds/microseconds, with `start_frame` for scheduled ISO
transfers. Neither is an original vendor-EP0 SOF timestamp. Submission/completion
tracing could diagnose scheduling overhead without closing physical phase.

[Ftrace's clock documentation](https://docs.kernel.org/trace/ftrace.html#the-file-system)
distinguishes per-CPU `local`, adjusted `mono` and unadjusted `mono_raw`. A
timestamp's clock must be recorded; selecting RAW does not move the timestamp
to a controller event. Any future perf/kprobe/eBPF feasibility experiment would
also need exact probe placement, lost-event/CPU/clock accounting and observer
costs. A function entry/exit bracket can at best enclose the software operation
it probes. It does not create a hardware cross timestamp. No tracing controls,
probes or new live kernel instrumentation are enabled by this prototype.

Generic I/O also has conditional
[`rwmmio_read`/`rwmmio_post_read` sites](https://github.com/torvalds/linux/blob/v6.18/include/asm-generic/io.h)
before/after MMIO access and ordering, with
[address/width/value records](https://github.com/torvalds/linux/blob/v6.18/include/trace/events/rwmmio.h).
They require compile-time `CONFIG_TRACE_MMIO_ACCESS`; architecture support
alone is insufficient. Their absence in an installed build cannot be repaired
by enabling a filter without a kernel rebuild. Even when present, these are
CPU-side MMIO observations, not USB wire timestamps. Configuration, compiled
instructions, actual trace clock, lost events and observer costs must all be
validated before proposing an association. No such hooks are enabled here.

The evidence-backed stock-kernel limit is therefore the complete read bracket
of the existing guarded runtime view. C removes Python inter-read work, but
the stock callback still reads eight registers and handles runtime-PM/seq-file
work; the first register's exact RAW position remains unknown within that
syscall. No reviewed public API or tracepoint closes that term to a qualified
submicrosecond host timestamp. A narrower diagnostic would require additional
validated observability, followed separately by physical phase validation.
The [first-register feasibility proposal](usb-host-timestamp-proposal.md)
specifies the exact build/instruction/clock evidence and a later discriminating
experiment, while leaving all live instrumentation disabled.

## Ledger

| Boundary / term | Observation and fence | Current uncertainty / user-space closure |
| --- | --- | --- |
| Accepted BLE source → T096 map | Full root/root boot/source epoch/path, generation, original issue/expiry and immutable map/sample agreement | Source fit residual is not RF/UTC bias. Unknown bounds stay null. Independent reference remains necessary. |
| Device SOF edge → 16 MHz capture tick | UFS1 or UTS1 captured frame/tick/current, USB/capture epochs, sequence/loss/race metadata | 62.5 ns timer grid is resolution, not absolute accuracy. Device event/PHY bias is uncalibrated. Counter/frame discontinuity withdraws. |
| Full-speed frame → host microframe | Device frame is modulo 2048; MFINDEX modulo 16384 | Direct-root topology is required for exploration. The integrated TT still exists. IN brackets constrain conditional integer-origin candidates; they do not prove phase or numbering. No guessed origin supplies time. |
| MFINDEX → RAW | Existing read-only reg-runtime, before/after RAW clocks; adjacent value changes | Transition lies after the earlier read starts and before the later read ends. Keep that full interval, skipped transitions and read load. Midpoint fits are diagnostics; unknown hardware phase remains. |
| Polling / request scheduling | Actual cadence, partial attempts, elapsed time and process CPU retained | Burst ≤24 reads or 500 µs; ≤1024 register reads/s; CPUs0–2. Scheduling broadens intervals. A minimum observed bracket is not a worst-case bound. |
| Vendor request → packet capture | Bounded 0xc1 IN0x53/0x52/0x51 on exclusively claimed timing interface | Captured UTC belongs to the device tick. Whole RAW/realtime request intervals bound current-counter comparison. Arrival is never the event timestamp. |
| RAW ↔ MONOTONIC / BOOTTIME | Nested integer RAW brackets around all clock reads | RAW avoids chrony's frequency steering; MONOTONIC/realtime do not. BOOTTIME−MONOTONIC changes fence suspend. Report actual read widths; no atomic cross-clock claim. |
| RAW ↔ system UTC / TAI | Separate realtime and kernel TAI reads inside saved RAW brackets | System UTC is already M-HAT disciplined; comparison has shared feedback. TAI is observed dynamically, never hard-coded. Steps/incoherent brackets reject. |
| USB topology / controller / cable / TT | Exact sysfs ancestry, speed, port path and driver, existing debugfs identity | Direct full-speed cable avoids an external hub but not integrated TT/PHY terms. Cable and controller-to-wire delay/bias remain unknown. No topology change carries an old fit. |
| Source map → source monotonic ns | Exact integer rate/phase arithmetic; source epoch/path and generation fences | UTC translation excluded from relative fits. Same generation cannot mutate. Old/retained maps cannot grant a new permission. |
| Source monotonic → UTC label | Exported signed UTC offset preserved separately | PPS-to-UTC association and source/PHY bias remain unverified. Tiny source residual cannot close this term. |
| Disconnect / reset / suspend / re-enumeration | Exact USB serial/silicon, host boot, USB/capture epoch, active controller and stable enumeration | Stop and release; no automatic reset, driver detach or reconnect. Sequence replay, gaps>500ms and counter discontinuity reset training. |
| UTE1 / missing SOF | Separate decoded nonmeasurement types, no reconstructed map | UTE1 retained estimates and reason7 classification never feed a reference or refresh a deadline. This prototype conservatively restarts strict training on unavailable samples. |
| UFA1 marker → host origin | Explicit optional1Hz zero-data OUT0x54 with nonzero token, IN0x55, full identity/epochs/sequence/counter/frame and original OUT bracket | Only a transient diagnostic tuple changes. Sixteen consistent markers can constrain integer origin under a declared short1000ppm extrapolation envelope. This is not a calibrated physical phase or UTC bound; contradictions block the epoch. |
| M-HAT control → kernel GPIO PPS | Existing collector and cached sysfs assertions; no UART reader | IRQ timestamps include capture latency and clock feedback. Seed is not a new pulse; skipped sequences/period gaps remain explicit. Electrical/UTC error remains unknown. |
| Source-path comparison | Nearby full GM/root/boot/epoch match where available, parent/path kept separate | Snapshots only describe lineage; they cannot authenticate exact event ancestry. USB relay and M-HAT path delays may differ. Do not pool unlike paths or epochs. |

The user-space closable terms are strict identity, immutable-map arithmetic,
original deadlines, bounded request/clock reads, unique short-gap unwrap,
observable suspend/enumeration fences, causal fitting, observer accounting and
separate populations. Physical frame phase, host/device PHY delay and UTC bias
cannot be certified from those data alone. A stock diagnostic counter may
improve relative association without providing an atomic hardware timestamp.

## Causal interval models

The offline `interval_report.py` replays every observed host transition through
two set-membership models. Both predict the current transition from preceding
observations **before** incorporating its full new bracket. They keep skipped
transitions, reset failures and the complete out-of-sample denominator.

The bounded-rate model propagates a feasible phase interval with arbitrary
per-microframe rate changes within a declared RAW/USB **±1000ppm scenario**.
Integer endpoints round outward and include1ns at each read bound for clock
representation. Intersecting with the next measured bracket refines the next
state. A disjoint observation remains a failure and starts a new state; it is
never trimmed. No constant rate or fitted residual is needed for this model.

The separate affine trial retains all periods/intercepts compatible with the
preceding at most64 transition brackets, within the same rate scenario. It
starts prediction after16 prior transitions. Its short-window constant-period
assumption is stronger and explicitly testable; a contradiction is retained
and resets the next fit. It does not transform a small residual into a bound.

The1000ppm ratio is an analysis scenario, **not a calibrated Pi oscillator
bound**. USB's frame timing requirements alone cannot calibrate the separate
Linux RAW clock. Linux's [clocksource documentation](https://docs.kernel.org/timers/timekeeping.html)
describes hardware counters and their frequency-to-nanosecond conversion, not
this board's physical clock accuracy. `clock_getres` is also not a frequency
or physical phase calibration. The report therefore leaves absolute uncertainty
null even if every subsequent bracket intersects a prediction.

Coverage has two distinct reported meanings: whether the predicted set overlaps
the complete observation bracket, and whether it contains **the entire**
bracket. Overlap only means the model has not been falsified; the actual event
is unknown within that bracket. The report also retains disjoint distances,
predicted widths and the midpoint OLS prediction's worst distance to either
observed endpoint. Common OLS/model denominators are explicit; no tail is
removed to improve coverage or RMS. These models exclude controller/PHY bias.

## Device frame-label coherence

The host checks captured-frame increments against the captured-counter delta,
independently of UTS1's valid-map flag. A packet can have an exact source map
at its captured tick while its frame label disagrees with that tick. Stable
double reads and an age just below16000ticks do not themselves prove those
two hardware fields describe the same SOF. Such packets remain explicit
counter/frame rejections and restart training; they do not enter admitted
shadow-offset comparisons. Their original packets and candidate offsets remain
in the withheld population.

Nordic's [nRF52840 USBD specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/usbd.html)
describes SOF events synchronized to16MHz before PPI and a readable frame
counter, but does not establish an atomic read association between FRAMECNTR
and a separate TIMER capture. A late-frame/new-label versus old-capture race is
a hypothesis requiring a discriminating measurement, not an attributed cause.
Synthetic boundary tests verify that the host rejects this class of mismatch.
No live firmware change is required to withhold it, and no automatic correction
or fixed one-frame subtraction is permitted.

The repeatable [`source_tests.py`](../../gateways/pi5/usb/source_tests.py) includes
the unchanged public source in a deterministic C fixture. Its18 cases document
the existing scalar guard, independently of host decoder tests:

| Synthetic input condition | Existing source behavior |
| --- | --- |
| Stable frame/CC, age0,15990 or15999ticks | Can publish a valid map when the original map permission applies. |
| Stable new frame label with old CC, age15990ticks | Also admitted; equality of repeated scalar reads does not prove association. |
| Stable old frame label with new CC, age3ticks | Also admitted; no previous frame/counter continuity is checked in this request path. |
| Age16000 or16001ticks; out-of-range frame | Rejected after the bounded retry. |
| Frame or CC changes visibly on the first snapshot | One retry can admit the following coherent scalar snapshot. |
| CC changes on both snapshots | Rejected; no fabricated event. |
| USB epoch changes while sampling, or capture epoch differs from map | Rejected, with no permission borrowed from another epoch. |
| Low32 capture wrap with coherent extended current | Extended event is reconstructed in the same capture epoch. |
| UFA1 stable mismatched tuple; later USB/capture epoch change | Scalar tuple can initially be admitted; immutable fetch rejects later epoch changes. |

These are modeled register sequences against actual serialization code, not
proof that the physical peripheral produces either stable ordering. The Nordic
documentation gives no atomic FRAMECNTR/TIMER latch contract or numerical
maximum skew here. A new firmware age exclusion would remove valid late-frame
events too and lacks a justified guard width. The current host preserves the
counter/frame discrepancy as a rejection, its candidate offset as withheld
diagnostics, and its lost availability/training denominator. It does not trim
those tails into an apparently better admitted RMS.

Device frame unwrap uses the independently captured counter for cadence.
Request submission and completion only bracket transport and enforce a
separate500ms host observation gap. Transfer scheduling can vary by several
milliseconds; comparing device frame cadence with request submission would
create false association failures even for coherent frame/counter tuples.

A token/identity/epoch-matched UFA1 response with reason2 is an unavailable
snapshot, not a numbering contradiction. The observer retains it, discards the
preceding marker training set and requires16 fresh valid markers to constrain
origin again. Stale fetches, token/epoch mismatches, sequence replay and actual
candidate contradictions still block that marker scope. Unavailable current
UTS1 maps are likewise counted as validly decoded unavailability, separately
from wire rejection or a withheld clock/permission interval.

The independent raw-tuple diagnostic continues through map unavailability. It
compares each usable adjacent capture, including both transitions around a
disagreement, without assigning endpoint blame or granting time authority.
Those transition counts are not counts of proven bad packets. A
[device-side repair proposal](t096-sof-association-proposal.md) preserves this
distinction and requires separate implementation/installation review.

## Paired comparison contract

Begin with a prescribed modest-load baseline. Then select a fixed **600-second**
window after settling, independently of observed error, with the same authenticated
GM/root/boot/epoch where verifiable. Retain every attempted, valid, unavailable,
rejected, warmup and predicted observation; all source/clock changes; gaps and
capture failures. Report bias, RMS, population SD, nearest-rank absolute P99,
maximum and strict >1/2/5 µs tails separately for each meaningful metric/cohort.
Preserve original raw integer timestamps and complete fit prefixes for replay.

The prototype's readiness criterion is a separately recorded continuous
**60-second** paired observation at the same prescribed load, with all source
maps admitted, sampled lineage matched, eligible M-HAT status/PPS and consistent
markers. It differs from the historical paper's120-second settle. Preserve that
protocol difference; never retrospectively discard early tails or restart an
already selected fixed window to imitate the historical protocol.

The simultaneous M-HAT/GPIO population is the control. The separately frozen
[published baseline](../../evidence/pi5/2026-11-02/README.md) had 600/600 new
pulses, raw phase RMS872.002ns, absolute P99 4809ns and maximum7017ns. It is a
different fixed window, not a substitute for the simultaneous control. The
[historical macOS comparison](../../evidence/usb/2026-10-17/README.md) uses another
host association and evidence population. Source/SOF-ordinal residuals,
MFINDEX midpoint innovations, USB/system intervals, GPIO phase and chrony RMS
are reported separately. None may be pooled or ranked as equivalent accuracy.

USB parity requires a defensible causal host association and comparable
event/clock/bias bounds, together with availability and all tails. A small
conditional residual or unknown frame-origin bias cannot pass. Keep the named
future chrony source `PUSB` disabled until a separate promotion review; the
existing chronyd remains the only system-clock writer.

## Token-bound request enclosures

The saved-data [`origin_bounds.py`](../../gateways/pi5/usb/origin_bounds.py)
tests a combined device-capture/controller-origin interval without assigning
an event timestamp to a USB arrival. Its conservative capture bracket starts
before the zero-data UFA1 OUT and ends after the matching token's IN completes.
It subtracts the original recorded SOF age with exact half-nanosecond arithmetic,
an explicitly uncalibrated±1000ppm age-rate scenario and counter/grid margin.
Only complete MFINDEX transition brackets preceding the OUT train the host
affine model. Separate integer-frame alternatives remain visible.

A tighter OUT-only alternative needs capture-before-status ordering. The
pinned Zephyr4.1.0
[`usb_handle_control_transfer`](https://github.com/zephyrproject-rtos/zephyr/blob/7823374e872145b5bd018bfe447839eb36042611/subsys/usb/device/usb_device.c)
calls the vendor handler before `usb_data_to_host`. The pinned
[`usb_dc_nrfx.c`](https://github.com/zephyrproject-rtos/zephyr/blob/7823374e872145b5bd018bfe447839eb36042611/drivers/usb/device/usb_dc_nrfx.c)
calls the setup callback synchronously; its zero-data OUT status write invokes
`nrf_usbd_common_setup_clear` after that callback enters the stack. The
repository's hash-pinned EP0 patch retains this order and adds generation
checks. The pinned
[`nrf_usbd_common.c`](https://github.com/zephyrproject-rtos/zephyr/blob/7823374e872145b5bd018bfe447839eb36042611/drivers/usb/common/nrf_usbd_common/nrf_usbd_common.c)
implements that clear by triggering EP0STATUS. Nordic's
[control-transfer description](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/usbd.html)
states that software enables the status stage. The documented
[anomaly154](https://docs.nordicsemi.com/r/bundle/errata_nrf52840_enga/page/err/nrf52840/engineeringa/latest/anomaly_840_154.html)
requires a preceding data stage and does not describe this zero-data request.
These are source-level and documented semantics, not an electrical measurement
or universal guarantee for another installed dependency build. Bind the exact
compiled driver/stack hashes to an actual installation receipt before applying
even this conditional ordering argument.

An exact-function offline fixture also exposes a separate C-language hazard:
the current marker returns `data=NULL,length=0`, after which the pinned stack's
`usb_data_to_host` performs pointer arithmetic on that null pointer. Full UBSan
rejects it. A call-order check with pointer-overflow sanitization explicitly
excluded can test callback-before-status, but is not a full sanitizer pass.
Its compiled-device effect is unmeasured. A future separately reviewed repair
could return a stable nonnull pointer for zero-length replies; this observer
does not change firmware or infer a present ordering failure from that finding.

Constant-origin set intersections are retrospective hypothesis constraints.
Every current interval is scored against the preceding set first; warmup,
contradictions, overlap and whole-interval containment have separate counts.
An overlap does not measure actual event coverage. Without an independently
justified physical-bias prior, changing the integer-frame origin can be absorbed
by a different combined bias. A narrow set therefore cannot independently pick
the correct frame origin, calibrate UTC or grant source authority. The frozen
provisional shadow offset is retained and is not applied to RAW-domain origin
intervals.

The next discriminating measurement is a causal integer-origin witness narrower
than one full-speed frame, plus repeated bracketed MFINDEX transitions against
RAW under declared load. Existing UFA1 diagnostic markers can constrain origin
in explicit `--markers` mode when its scope permits the transient handshake;
default capture remains IN-only. An independent
electrical/reference observation is still required to close controller/PHY and
UTC phase bias. Better relative fits alone do not resolve those terms.
The [independent-witness design](usb-integer-association-witness.md) explains
which discrete event evidence could distinguish an initial or persistent
integer-label shift without a fitted phase or UTC shortcut. Its implementation
and hardware operations remain deferred.
