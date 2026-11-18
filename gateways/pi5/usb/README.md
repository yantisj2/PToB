# Linux USB shadow timing observer

Copyright (c) 2026 Jonathan Yantis. MIT.

This experimental shadow observer pairs a verified T096 RC USB source with
the Pi gateway's existing M-HAT/GPIO control path. It records timing contracts,
clock-read intervals, stock xHCI MFINDEX read brackets, cached PPS assertions
and the existing collector's status. It never feeds chrony or adjusts a clock.
The [research ledger](../../../docs/pi5/usb-sof-research.md) explains the remaining
host frame-origin, controller/PHY and UTC uncertainties.

Read the [Pi setup](../../../docs/pi5/setup.md),
[USB wire contract](../../../clients/macos/usb/README.md) and
[clock ownership guide](../../../docs/pi5/clock-chain.md) first. Live capture
requires separately authorized exact-device access through the existing pinned
SSH profile. Run the offline checks while the Pi is unavailable.

## Offline verification

From the repository root:

```sh
python3 -B -m unittest gateways.pi5.usb.test_shadow gateways.pi5.usb.test_reporting_gate -v
python3 -B gateways/pi5/usb/source_tests.py --out artifacts/usb-source-boundary-check
python3 -B gateways/pi5/usb/runtime_tests.py --out artifacts/usb-native-runtime-check
```

The Python tests use the standard library and synthetic fixtures. They check wire
decoding, source identity and original expiry, causal association, reporting
readiness and retained gaps. The source boundary runner uses Clang (or `CC`) and
UBSan with the public T096 test fakes. The native runtime runner uses `cc` and
ordinary file fixtures. Both write receipts to new directories under ignored
`artifacts/`. Pass `--sanitizer address,undefined` to the source boundary runner
for a separate ASan and UBSan run.

## Bounded paired capture

Provide the full discovered serial, including its factory silicon suffix. A
display alias is insufficient. Store the SSH profile and any device-specific
calibration outside the checkout. Keep raw reports and replay outputs under
ignored `artifacts/`. The output parent must already exist and the output name
must be unused.

```sh
mkdir -p artifacts/pi5/usb-shadow
python3 gateways/pi5/usb/shadow.py --capture \
  --serial FULL_USB_SERIAL --seconds 60 --rate 8 \
  --out artifacts/pi5/usb-shadow/baseline.json
```

The guarded remote call verifies the enrolled Pi model/factory/image and SSH
host pin. Capture verifies a unique USB silicon identity, VID/PID `2fe3/0100`,
one `ff/54/01` vendor interface and no existing usbfs/CDC owner. It exclusively
claims that vendor interface, leaves CDC bound and releases its claim at exit.
It stops on transport or enumeration changes without automatic reset/rebind.
It does not open UART or CDC, mount debugfs, load modules or alter service/IRQ,
power, firmware, driver or time configuration. It creates only a private
cooperative lock under `/run/lock/ptob-usb/` on the Pi.

Default host observation requires a direct 12 Mbps device on an already active
Synopsys DWC3/xHCI controller whose existing debugfs directory matches its
sysfs ancestry. The only register file opened is `reg-runtime`, read-only. That
stock view reads eight runtime registers and internally takes runtime-PM
references. Each burst is capped at 24 reads or 500 µs, with at most 1024 reads
per second; the process stays on CPUs 0–2. The report records actual CPU use,
read counts, temperature, IRQ affinity and clock processes. Debugfs is an
unstable diagnostic ABI; a missing or changed format fails closed. Use
`--no-mfindex` for a coarse IN-only control population without register reads.

Start with 8 Hz for 60 seconds. A healthy baseline can precede a separately
named fixed 600-second window at 8, 16 or 32 Hz. Keep attempted, unavailable,
rejected and warmup observations, every tail, and all capture failures. Do not
extend a failed window or select a favorable tail. Partial attempts and failure
receipts are retained. The first cached PPS assertion is a seed, not a new edge.

The report's `settlement` result uses60seconds of continuous source permission,
matched sampled GM/root/boot/epoch, eligible M-HAT status/PPS and a constrained
marker origin to identify readiness for a separately named fixed600s candidate.
It never selects on residual or offset magnitude, extends a failed window,
starts a new capture automatically or promotes a source. Unavailable readiness
populations remain separate evidence.

## Replay and interpretation

```sh
python3 gateways/pi5/usb/shadow.py \
  --analyse artifacts/pi5/usb-shadow/baseline.json \
  --out artifacts/pi5/usb-shadow/replay.json
python3 gateways/pi5/usb/comparison.py artifacts/pi5/usb-shadow/replay.json \
  --out artifacts/pi5/usb-shadow/comparison.md
```

Replay is offline. Source time uses exact immutable-map arithmetic; UTC labels
and counters remain integers. Original permission expires from request start,
with no renewed authority from arrivals, local estimates, missing-SOF records
or presentation history. UTE1 is decoded for compatibility and never becomes a
measurement. Default capture is IN-only; optional UFA1 markers use a separate
explicit mode described below.

The replay freezes public analyzer sources and their hashes in a separate
`.implementation.json` sidecar before execution. The comparison report keeps
attempted, admitted, warmup, predicted and withheld diagnostic denominators,
with cohorts and unlike metrics separate. A valid-map flag alone does not admit
an incoherent frame/counter pair. Source cadence uses captured device counters,
with a separate host gap fence, rather than request submission/arrival time.

Keep syscall experiments separate: `--host-read-method pread` uses a single
positional read at offset zero; the default remains `seek-read`. Record actual
freshness, full brackets and observer load. Both paths still read the stock
eight-register runtime view; neither provides an atomic frame timestamp.

`--host-read-method native-pread` explicitly builds a temporary ordinary C
user-space library with the Pi's existing `cc`. Its only hardware-facing
operation is `pread` on the already-open, read-only guarded runtime file. It
reads RAW clocks and parses frames inside the same bounded burst, removing
Python allocation/regex/loop gaps between reads. It does not install a compiler,
library, daemon or kernel component. Compile commands, source/library hashes,
startup wall/child CPU, cleanup and partial error brackets remain in the report.
The temporary library is removed at exit. Missing compiler or malformed reads
fail closed. Native parser/budget tests use ordinary synthetic files only.

`--realtime-bracket-method native-pair` is a separate opt-in experiment requiring
`--host-read-method native-pread`. It reads RAW→REALTIME→RAW in C, preserving the
new realtime sample as an integer inside its own RAW sub-bracket. Every full
five-clock snapshot and the old full-bracket phase diagnostic remain present.
Reports compare the two RAW associations using the same event, preceding host
model, realtime sample and common denominator. They retain raw and fixed-offset
cohorts, withheld diagnostics and both bracket widths. Legacy captures have no
sub-bracket to reconstruct. A smaller cross-clock bracket does not timestamp
MFINDEX, bound controller/wire phase, or justify changing the frozen correction.
Clock failures and RAW rollback fail closed; cleanup uses the ordinary full
clock path so helper errors do not prevent interface release. The default is
`legacy-wide`; method transfer is unqualified.

`--host-warmup-seconds 5` explicitly records five seconds of host-only8Hz
preparation before the fixed acquisition window. Its raw bursts and CPU/read
costs remain separate and visible; no USB source requests occur in preparation.
Replay seeds only the preceding host model from those reads and refuses future
preparation timestamps. Source admission,64-point source warmup, UFA1 association
and original expiry remain independent. The default is zero preparation seconds.

## Provisional shadow correction and received-time results

The bundled [`provisional-calibration.json`](provisional-calibration.json)
applies a frozen additive **−25,600ns** to derived shadow UTC and phase outputs.
Its basis is the first admitted phase cohort A in the named8Hz600s population:
648 samples with+25,590.433ns mean against the M-HAT-disciplined Pi. The correction
is provisional and dependent on that clock. It does not change raw packets,
maps, source admission, original deadlines, chrony, PHC or PPS/PTP services.
Absolute uncertainty remains unknown. It is not retuned for later windows,
source cohorts or native read methods; method transfer remains unqualified.

Use `--calibration PRIVATE_FROZEN_JSON` to add an exact silicon/training-scope
binding, or `--no-calibration` to explicitly disable the derived correction.
Configuration validation preserves the frozen value/sign, original basis,
controller/topology/method scope and unknown uncertainty. Every admitted and
withheld phase cohort retains raw and corrected bias, SD, RMS, absolute P99/max
and strict tails. Near1ms rejected diagnostics remain visible.

Host-phase diagnostics labeled `ADMITTED` include source `WARMUP` rows; they
are separate from source causal forecasts. Consecutive shifted frame labels
can have a coherent interior after a source-model fence, so such an interior
can retain a near1ms host diagnostic during warmup. Keep it in the admitted
host diagnostic denominator, with its source state and the neighboring
withheld transitions. Neither the label nor a new warmup seed certifies the
physical event or grants clock authority.

An optional offline reporting preview adds a separate conditional series:

```sh
python3 -B -m gateways.pi5.usb.reporting_gate artifacts/pi5/usb-shadow/baseline.json \
  --out artifacts/pi5/usb-shadow/reporting-preview.json
python3 -B -m unittest gateways.pi5.usb.test_shadow gateways.pi5.usb.test_reporting_gate
```

It consumes the capture's retained strict analysis and leaves all historical
labels, values and calibration intact. Report readiness requires fresh original
source permission, the existing64-point source model's causal-ready state, a
native host diagnostic and conditional same-physical-scope marker association
whose IN completion is at most1.5s old. It then requires at least1s and nine
consecutive ready observations. The elapsed interval uses request RAW clocks,
not an event timestamp. Valid advancing map generations do not restart it.
Capture start, full source/boot/path/USB/capture context changes, unavailable or
withheld source, expiry, missing data, clock boundaries, gaps and association
loss restart readiness. It re-arms after late interruptions as well as startup.

While blocked, qualified-series values are `null` with explicit reasons and
counts; charts must preserve those gaps and never zero-fill or backfill them.
JSON retains every raw diagnostic, historical admitted population, new
report-ready subset and gap run per cohort. This preview changes no capture
default, live guard, dashboard, clock feed or source permission. It selects by
readiness rather than phase magnitude and cannot resolve an initially or
persistently wrong integer origin, physical bias or UTC accuracy. Report-ready
means conditional shadow presentation only; it is not parity qualification.

Received-time quality does not wait for a continuous600s window:

```sh
python3 -B gateways/pi5/usb/received.py artifacts/pi5/usb-shadow/baseline.json \
  artifacts/pi5/usb-shadow/next-capture.json \
  --out artifacts/pi5/usb-shadow/received-results.json
```

This saved-data-only command freezes analyzer/config sources, writes JSON and
Markdown, rejects duplicate/overlapping populations and reports every capture,
authenticated source cohort, received interval, warmup, unavailable state and
rejection. It pools availability counts only; quality metrics stay per capture,
cohort and method. Sampled-root GPIO subsets remain distinct from exact-event
lineage and from periods when USB authority was unavailable. The60s/600s gates
apply to eventual qualification and never suppress useful partial results.

## Experimental causal host filtering

`--host-filter robust-host-v1` explicitly enables a separate host-only
experiment in capture/replay and `received.py`. The default remains the original
unfiltered causal OLS diagnostic. This experiment fits only preceding complete
MFINDEX/RAW transition brackets:64–128 training points, a robust affine fit,
past-only width/innovation thresholds, and a fresh warmup after four consecutive
training failures. Every current innovation is scored **before** deciding
whether it may train the next fit. All excluded forecasts, warmup observations,
widest brackets, coverage and tails remain in the report.

The report compares the same timestamps and denominators with the original
64–1024-point OLS and a64–128-point OLS control. It also shows matched selected
subsets separately; a favorable selected subset is not the all-forecast result.
Conditional phase outputs retain raw OLS, provisional-offset OLS, filtered and
filtered-plus-fixed-offset variants per source cohort, including withheld
frame/counter diagnostics. Filtering never changes source permission, source
warmup, raw maps or expiry, and does not learn USB/M-HAT phase or retune the
frozen correction.

The compatible macOS lesson is preceding-model prediction with strict immutable
identity/expiry fences and all innovations retained. This robust host experiment
is new; macOS's local UTE presentation smoothing is not imported as measurement
authority. Synthetic phase-step/drift/gap/source-change tests expose recovery
lag. Recovery is observation dependent:64 fresh points can take seconds at
the modest polling rate. No smoothing residual supplies physical accuracy or
a finite controller/PHY uncertainty bound. Keep the experiment opt-in unless
separate all-forecast and recovery evidence supports it.

The separate `--host-filter weighted-host-v2` experiment uses64–1024 preceding
host points, two Huber reweighting passes, a fixed3×scaled-MAD cutoff and500ns
scale floor. Every point remains in training; there is no width/innovation
exclusion. Its control uses the same maximum window. Current errors are still
scored against the preceding model before adding the observation. The constants
are experimental sensitivity choices, not calibrated uncertainty bounds.
Keep P99, maximum, strict tails and phase/rate-step lag visible; lower RMS alone
does not establish improvement. A first error crossing below1µs is distinct
from16 consecutive qualifying forecasts or eventual settling. Filter selection
never changes source admission or the fixed provisional correction.

For a saved native-pair capture, `clock_pair_filters.py INPUT --out NEW_JSON`
compares both unchanged experimental filters with the native realtime
association. It applies the same integer RAW midpoint shift to each preceding
host forecast and its OLS control, retaining all raw and fixed-offset phase
outputs. Reports show each filter's matched events, common events across both
filters, missing forecasts and the separately withheld anomaly population.
The command opens no hardware and never infers missing native brackets in
legacy captures. Optional `--calibration PRIVATE_FROZEN_JSON` retains exact
private scope. Better selected RMS cannot justify a filter when coverage,
P99/max/strict tails or step/recovery response regress.

`interval_windows.py INPUT --out NEW_JSON` tests complete host-frame enclosure
sensitivity to64/128/256/512 preceding transition windows, with16-point minimum
and an explicit uncalibrated±1000ppm rate scenario. It records the actual point
counts/time spans, all predictions, missing forecasts, disjoint observations,
resets and common acquisition denominators. Longer constant-rate history is a
stronger assumption; overlap with a read bracket does not establish actual
event coverage. This saved-data-only experiment changes no source/host default
or calibration and never turns an enclosure into a physical/UTC bound.

`origin_bounds.py INPUT --out NEW_JSON` uses saved token-bound UFA1 exchanges
to test a separate combined controller/SOF-capture origin hypothesis. The full
OUT-start→matching-IN-completion interval encloses the snapshot; an explicitly
conditional OUT-only alternative requires separate completion-order proof.
Recorded SOF age, tick/grid margin and an uncalibrated rate scenario project
the original counter timestamp. Only preceding MFINDEX brackets train its
host model. All clock/identity/token/epoch guards, initial warmup, contradictions
and integer-origin alternatives remain visible. Constant-bias intersections are
retrospective and never retune the frozen shadow correction or choose an
integer origin without a physical bias prior. This clock-only diagnostic can
run on saved markers during source loss; it supplies no UTC map or permission.
The [ordering record](../../../docs/pi5/usb-sof-research.md#token-bound-request-enclosures)
describes exact installed dependency provenance requirements and a retained
full-UBSan zero-length/null-pointer finding. It does not alter firmware or
qualify the conditional OUT-only bound.

`filter_recovery.py --out artifacts/pi5/usb-shadow/synthetic-recovery.json`
reproduces the public synthetic drift, phase/rate step, observation-gap and
source-label step scenarios. They declare a polling cadence and known synthetic
truth, retain every failed/missing forecast and distinguish an initially
correct rate-step forecast from recovery after subsequent lag. They are not
hardware observations or accuracy validation.
Use `--host-filter weighted-host-v2` with that command to evaluate the longer
window; the default synthetic report evaluates `robust-host-v1`.

## Optional local-estimate diagnostics

The separately labeled `--local-estimate-diagnostic` option adds one256-byte
vendor-IN0xc1/request0x57 per second. It uses the already claimed exact timing
interface and retains the same ownership, silicon/MCU boot and transport-stop
guards. It does not open CDC or send another OUT command. Keep this additional
request load and its sequential relationship to UTS1 source/current polls in
the population metadata.

UTE1 is always local presentation diagnostics. Reports expose its counter-valid,
estimate-present, session-current and root-envelope-current flags, retained
source/boot/path identity, original discipline/session-until ticks and request
clock brackets. These can distinguish an available local estimate from strict
map availability or a noncurrent session/root envelope. A false current flag
does not by itself identify an RF/GNSS cause. Positive remaining ticks also do
not grant permission when a current flag is false. UTE1 never becomes a source
measurement, derived corrected UTC sample, chrony input, permission renewal or
substitute for an unavailable UTS1 map. Malformed flags/counters/identity stop
capture and retain a failure receipt.

`local_diagnostic.py INPUT... --out artifacts/pi5/usb-shadow/envelope-margins.json`
replays original UTE1 records and saves exact integer retained-map projections
and issued/expires/session margins. These values remain diagnostic: they do
not extend strict map intervals, produce a usable UTC timestamp or restore
source permission. Both current flags depend on the device's published
`estimate_is_current` gate. The report therefore separates arithmetic root
interval membership, session tick margins and the current gate; a cleared bit
alone is not proof of deadline expiry or an RF/GNSS cause. The source snapshot
and original input hashes are frozen before this offline derivation.

New captures explicitly begin acquisition after preparation and end before
cleanup, with separate `preparation` and `observer_total` records for all
capture-function self/child costs including compiler/environment tasks. Earlier
receipts retain their original boundaries; do not rewrite them for comparison.
The500µs burst threshold is checked between reads; an individual delayed syscall
can exceed it and remains a retained tail. The24-read and1024-read/s limits are
hard limits. Native observation still executes all eight stock register reads
and cannot remove unknown timestamp position within that callback or PHY bias.

Capture saves the exact executed public Python payload beside each raw report
as `NAME.remote.py`, and verifies its hash returned by the guarded Pi. Strict
wire decoding runs during capture as well as replay. A short response, wrong
silicon/contract/MCU boot or marker token stops the reader and releases its
claim. A1Hz ownership audit permits only the process's own usbfs FD; an external
USB/CDC owner stops capture without seizing it.

The source/SOF-ordinal causal residual measures corrected source consistency
against device-observed USB frame cadence. It omits host-clock phase and is
**not equivalent** to macOS RX USB RMS or GPIO PPS phase. MFINDEX transition
midpoints have measured outer brackets and unknown controller-to-wire bias.
The current-counter USB-minus-system comparison preserves the entire request
interval. It does not treat request/arrival midpoints as event timestamps.

An additional **conditional spec-mapped shadow** uses xHCI4.14.2's frame-number
mapping and the preceding host transition fit to estimate the source event in
RAW time. It reports corrected-source causal errors and USB-minus-system offset
per source cohort. Its host phase is an estimate with unqualified physical bias;
it is not an atomic timestamp or the macOS metric and never supplies chrony.

The system clock is disciplined by the simultaneous M-HAT control. Agreement
with it is a relative shadow comparison. Matching nearby GM/root/boot/epoch
snapshots do not prove exact-event lineage, and distinct relay paths retain
different delays. No USB/M-HAT parity, absolute UTC accuracy, continuous source
availability or qualified failover is inferred from a fit residual.

`chrony-shadow.conf.example` reserves the distinct proposed `PUSB` source name.
It contains comments only. No socket emitter or promotion switch is implemented;
unknown host association remains a hard gate.

## Explicit transient frame-origin markers

When the authorized hardware scope includes the existing diagnostic marker,
make a separate, modest-load candidate with `--markers`:

```sh
python3 gateways/pi5/usb/shadow.py --capture --markers \
  --serial FULL_USB_SERIAL --seconds 60 --rate 8 \
  --out artifacts/pi5/usb-shadow/marker-candidate.json
```

This mode adds only one 1 Hz handshake: zero-data interface OUT `0x41/0x54`,
with a nonzero per-request token, then IN `0xc1/0x55` fetching its 64-byte UFA1
tuple. The existing firmware records a transient snapshot/sequence; it changes
no role, source map or persistent setting. The transport exposes no generic OUT
API. It never retries a failed OUT or reads a different token.

The decoder binds full silicon, MCU boot, USB/capture epoch, sequence/token,
counter/frame continuity and the original OUT clock bracket. The IN arrival
does not widen or replace that witness. Preceding bracketed host transitions
constrain integer-frame candidates with a declared short1000ppm extrapolation
envelope. Sixteen consistent markers and a unique candidate can constrain the
integer origin; physical phase, controller/PHY bias and absolute uncertainty
remain unqualified. A contradiction blocks that epoch. Markers do not grant
source authority and can test association during upstream loss.

The default remains IN-only. It omits sparse UFS1 raw diagnostic requests:
that separate firmware observer deliberately fences the USB epoch after a
consumer stall longer than one second, even if UTS1 requests remain healthy.
UTS1 already carries the source SOF capture. Raw UFS1 decoding stays available
for distinct diagnostic populations and offline fixtures.

## Complete-bracket interval replay

```sh
python3 gateways/pi5/usb/interval_report.py \
  artifacts/pi5/usb-shadow/baseline.json \
  --out artifacts/pi5/usb-shadow/host-intervals.json
```

This offline report compares a bounded-rate feasible phase interval and a
64-transition affine feasible set with the preceding-fit OLS midpoint
diagnostic. It retains every prediction, miss and tail; reports interval overlap
and full-bracket containment separately; and shows common denominators and worst
endpoint distances. Its±1000ppm RAW/USB ratio and short affine clock model are
declared analysis assumptions, not physical calibration. Absolute uncertainty
stays null. See the [ledger](../../../docs/pi5/usb-sof-research.md#causal-interval-models).

The [first-register proposal](../../../docs/pi5/usb-host-timestamp-proposal.md)
keeps any finer stock-kernel observability work offline. `host_probe_plan.py`
validates a pinned recorded instruction layout and assumed probe-hit budgets;
it neither installs a probe nor qualifies probe safety, a timestamp or physical
phase. Its ordinary verification uses public synthetic fixtures. Exact kernel
fragments/identities remain ignored evidence and are never operating commands.
