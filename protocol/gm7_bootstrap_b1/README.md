# GM7 bootstrap endpoint amendment B1

Copyright (c) 2026 Jonathan Yantis. MIT.

**CLEAR for functional offline receiver implementation, subject to the conditions
below. Not flash approval or hardware acceptance.** This named amendment permits
conservative local host endpoints for connectionless bootstrap where the platform
cannot expose exact advertising RF TX timestamps. It changes endpoint admission
only, not any wire layout. It binds implementation2 and scheduler A1 unchanged.

## Endpoint contract and proof

Capture local monotonic lower bound L before the fresh nonce-bearing request can
be submitted, queued or exposed to any transmitter. Allocate its pending record
and freeze deadline D=L+250000us. Queueing, retries, a TX callback or advertising
completion MUST NOT move L or D. A new retry is a new nonce/request under A1's
rate/phase limits; repeated transmission of an old request preserves L and D.
A cancelled/expired nonce cannot become pending again.

Capture monotonic upper bound U after the complete response has been received and
authenticated, or use a correctly associated actual RX END with an independently
checked current-processing deadline. The simple reference uses post-authentication
U so all queue, receive and processing time consumes the250ms budget. Require
L<=U<=D. Round timestamp uncertainty outward, never toward a tighter interval.
Monotonic reset, wrap, clock-domain mismatch or endpoint association uncertainty
invalidates the pending request/session; fail closed. Do not substitute wall time.

The responder samples its monotonic S after actual receipt of this exact request
and before the immutable response may transmit. If real local TX is T and real
local response RX is R, then L<=T<=R<=U. Thus [L-S,U-S] encloses [T-S,R-S]. Use
this conservative offset interval, with implementation2's drift/width limits;
never use its midpoint as proven time. Existing treatment of oscillator drift
must cover endpoint quantization and the exchange interval; timestamps must share
the platform's documented monotonic clock domain.

If immutable response source permission V is remaining at sample S, subtract the
ENTIRE conservative elapsed U-L. Require V>U-L and V<=1500000us. Local expiry is
U+(V-(U-L))=L+V: host queueing/processing cannot extend permission. Server response
queueing after sample is included in elapsed; the server must not resample or
rewrite V to revive queued stale source evidence. Relay response V remains clipped
to original upstream/root permission and proof expiry. This proof supplies a
conservative causal enclosure, not calibrated UTC or physical timing precision.

## Required prerequisites and boundaries

Keep configured group/key, full peer/requester identity+boot, unpredictable128-bit
nonce, exact outstanding match, source/lineage descriptors, response age bounds
and consume-once validation. Verify MAC before accepting scope. An authenticated
matching response consumes the nonce even when its permission is exhausted;
forged/unrelated traffic must not consume it. Bound pending storage and processing.
This reference assumes authenticated scope is supplied by the production acceptor;
it is not a replacement MAC/nonce implementation.

Only bootstrap/coarse freshness may use HOST_CAUSAL_BOUND_NOT_RF endpoints. The
session must report its provenance and uncertainty honestly. Do not call L actual
RF start or U an RF capture, or derive precise phase, PA sample association, servo
permission, calibrated output quality or aligned HOT readiness from them. Actual
G1/GRv3 capture association remains required. T096 aligned control/HOT START/END,
first-transmission deadline checks and complete-event radio budgets are unchanged.
Server responders still need their valid receive→sample→immutable-send ordering.

Diagnostics distinguish lower/upper endpoint provenance, conservative elapsed,
remaining permission, queue/deadline rejection and session uncertainty; unavailable
actualRF timestamps remain unknown. These are bounded local JSON diagnostics,
not new packet fields. Report session establishment success separately from
precision lock. Lack of an exact ESP32 REQUEST RF timestamp is no longer itself
a functional bootstrap blocker if all these conditions are implemented and tested.

## Verification

Run `python3 -B protocol/gm7_bootstrap_b1/verify.py`. Four reference tests exercise
causal containment, queue/processing delay, original deadline boundaries, source
permission subtraction/exhaustion, scope failure, consume-once and monotonic reset.
Production exits additionally test MAC/nonce/group/boot mismatch, async callback
ownership, actual clock API quantization, requeued advertising copies and delayed
worker processing. No radio success, native build or hardware result is claimed.
