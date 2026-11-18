# Proposed T096 SOF association repair

Copyright (c) 2026 Jonathan Yantis. MIT.

This is an offline review proposal. It does not modify firmware, authorize an
installation or change the current macOS/USB contract. The
[uncertainty ledger](usb-sof-research.md) and
[repeatable boundary fixture](../../gateways/pi5/usb/tests/source_boundary.c)
describe the evidence and remaining hardware uncertainty.

The current scalar guard admits a stable frame label and stable captured CC
when their age is below16000ticks, including a synthetic new-label/old-CC tuple.
It rejects visible scalar transitions, ages16000+ and epoch changes. An exact
source-map projection does not detect a mislabeled capture: the map can agree
perfectly at the old tick. This admission condition is reproduced against
unchanged source; the physical ordering that produced observed mismatches is
still unproven. Nordic documents SOF synchronization to16MHz before PPI and a
readable FRAMECNTR, without an atomic FRAMECNTR/TIMER latch guarantee. See the
[nRF52840 USBD specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/usbd.html).

The capture adapter reads FRAMECNTR, captured CC3, current CC2, CC3 again and
FRAMECNTR again under its lock. The SOF→PPI→CC3 route remains autonomous while
that lock is held. Repeated scalar equality therefore does not establish that
the label and capture belong to the same hardware event. Separately verifying
that the USB vendor callback finishes before EP0 status enable constrains the
request bracket; it does not make this snapshot atomic. Bind both application
capture/source hashes and the compiled USB stack/driver to the actual installed
image before transferring either source-level argument.

The smallest proposed repair is a shared, epoch-scoped validated SOF-sample
helper used by UTS1, UFA1 and the raw observer. Before implementation, review
capture ownership and worker serialization so this introduces no timer/PPI
resource conflict or change to USB stack event ownership.

1. Keep the existing bounded scalar read/retry and age checks. Preserve raw
   readings and separate scalar-race, age, gap and association failures.
2. In a stable USB/capture epoch, compare each usable frame/counter pair with
   preceding captured evidence. Establish a fresh seed after a gap or context
   change; require new coherent evidence before declaring a SOF associated.
   Use an explicitly justified counter-rate tolerance and unambiguous frame
   wrap range. Do not use packet arrival or a source-map residual for this test.
3. Withhold ambiguous or disagreeing associations. A bounded retry may recover
   a later coherent tuple; it must not widen source permission, backdate a map,
   subtract one frame automatically or select one endpoint as physically
   correct. Preserve both adjacent transitions around a disagreement.
4. Route every SOF consumer through the same validation decision. In
   particular, a transient UFA1 marker must not validate a scalar tuple that
   UTS1 would reject. Preserve immutable token fetch and final epoch checks.
5. Keep original map generation, publication intervals and issue/expiry checks.
   A raw coherent SOF during loss of source authority supplies no UTC map.
   Preserve existing wire compatibility: an unavailable SOF can use the
   existing unavailable response; new fields require an explicit versioned
   contract. Review whether any proposed epoch transition changes existing
   macOS semantics before accepting it.

The acceptance fixture must cover stable label-first and capture-first cases,
the legitimate late part of a frame, ages15999/16000, visible transitions on
either scalar read, repeated inconsistency, low32 capture/frame wraps, actual
counter restart, suspend/reset/owner changes, USB/capture epoch races, map
publication/withdrawal/expiry and UFA1 token fetch. A disagreeing tuple must
never become an admitted time sample merely because its projected map agrees.
Keep expected unavailability and warmup denominators, including the additional
availability cost of conservative validation.

This continuity-based repair can detect inconsistent associations; it cannot
prove the initial association or a persistent integer offset from sparse reads.
It is a veto on an association, not a complete certificate for recovery. A
one-frame label step may disagree once and then become internally coherent.
Automatically reopening after a chosen number of coherent transitions would
miss that persistent offset. A conservative epoch latch avoids that reopening
but can withhold the remainder of a healthy epoch after one transient event;
it still cannot identify an initially consistent wrong offset. A recovery
proposal must explain its independent association evidence and preserve this
availability cost, rather than treating a new seed as proof of correctness.
The [independent-witness design](usb-integer-association-witness.md) separates a
wire frame identity from request tokens, scalar continuity and fitted phase,
and states the additional event-binding evidence needed for recovery.

The offline detection/recovery experiment should compare those two policies
with the existing control on identical synthetic and saved records. Score each
current tuple against preceding raw evidence before updating state. Retain all
source permission failures, source/physical context changes and original expiry
limits. Report initial qualification, every veto, clean-record withholding,
missed known synthetic anomalies, warmup and first eligible/forecast delays.
Synthetic cases need clean late-frame reads, transient label and counter
excursions, an initial wrong offset, a persistent label step, gaps, epoch/source
changes and expired source permission. Hardware false positives and misses
remain unknown without ground truth. Any threshold selected from development
captures is retrospective; predeclare a fixed future/held-out population before
claiming measured validation. Such a replay changes no live guard or firmware.

The decisive hardware test is a boundary-focused observation that records
FRAMECNTR before/after, both CC reads, current capture, epoch and the next
related transition, with an independent SOF/PPS reference if physical phase is
claimed. A new arbitrary near1ms exclusion lacks a documented guard width and
would discard legitimate late-frame samples too. Additional hardware latching
or capture resources would require a separate resource/timing design and
validation; they are not assumed available.

Before any future installation: implement and compare the proposed helper
offline against the unchanged-source fixture, review exact-device ownership,
installed source and recovery, run the required firmware/native checks, then
obtain separately scoped installation authority. Keep the current gateway
chrony/PPS/PTP configuration and selected publication evidence intact.
