# Changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

## 17 October 2026 — T096 precision scheduling candidate

T096 **0.15.70** gives reserved periodic TX/RX events precedence over admission,
discovery and connected control in the pinned controller. It retains native
arbitration between precision events and all radio assertions. An experimental
RC phase servo applies fractional 8 us adjustments inside the legal slot,
without recreating the train; stale or illegal placement still withdraws or
reacquires normally. New phase journal records distinguish placement failure,
adjustment, start, quality withdrawal and demand expiry.

Validate admissions before changing a live RC7 path or resetting its receiver.
Same-scope renewals retain pending original/follow-up associations, fit and
original map deadlines. USB queue-overflow restart now fences old EP0 events,
replays current VBUS/regulator readiness and tolerates duplicate READY events.
The separate indefinite DMA wait and the historical persistent USB blackout
are not established as repaired. Source/session expiry and USB freshness limits
are unchanged. New production-body sanitizer tests are included in the offline
suite; simulation/build success does not establish hardware durability or UTC
accuracy.

## 16 October 2026 — macOS guide organization

Restore the original animated client preview and give the macOS README dedicated
interface, usage, tracking-results, continuity and provenance sections. Integrate
the selected ChronyControl view with the bounded publication-check results,
keeping the later screenshot and earlier hardware populations distinct.

## 16 October 2026 — macOS documentation placement

Move the ChronyControl screenshot, author attribution and tracking/development
notes to the macOS client README. Keep a short client link on the main README.
The image, historical evidence and firmware behavior are unchanged.

## 16 October 2026 — tracking preview refresh

Replace the README preview with a user-selected cleaner approximately ten-minute
ChronyControl interval, retaining Bryan Christianson's attribution. Use a calmer
development-status caption focused on planned GM authentication and transient
reduction. Preserve the earlier screenshot and detailed failure populations;
this presentation update changes no firmware, logging or measurement claims.

## 16 October 2026 — timing-chain stability and continuity

This update integrates T096 **0.15.69**, W12 **0.1.7**, and the macOS client
continuity changes. It is an experimental source update. The
[engineering results](evidence/stability/2026-10-16/README.md) retain the failed
and incomplete populations alongside the bounded successes.

- T096: improve cold admission, connection cancellation and established-link
  diagnostics; reserve native controller RX headroom under mesh load. Request
  100 ms RC discovery, gate every RC advertisement on usable upstream precision,
  and reduce failed steady control attempts without slowing the 16 Hz precision
  train. Legacy 300 s renewal and 360 s admission lease remain.
- T096: retain a separately labelled local clock estimate through source loss;
  preserve original authentication, source expiry and downstream authority gates.
  Move USB projection and serialization outside the interrupt-masking map lock,
  with a generation check that refuses raced observations. Add bounded native
  receive-outcome, transition, watchdog and retained-fault diagnostics.
- macOS: display HOLDOVER / LOCAL ONLY with observation age and unknown error;
  preserve the owned chrony process across compatible quality loss while pausing
  measurements. Source/counter changes still fence training. Reuse USB buffers,
  coordinate readers with a per-silicon kernel lock, and rediscover after stale
  transport errors. Add opt-in two-hour capture and ChronyControl observation.
  The README includes a user-supplied ChronyControl tracking screenshot with
  unresolved spikes preserved and attribution to Bryan Christianson.
- W12: align RC control/admission behavior, retain an OLED-only HOLD estimate,
  and add bounded receive-loss diagnostics. Isolate large status snapshots and
  enforce a static main-stack budget after rejecting a stack-overflowing
  candidate. See the [W12 changelog](firmware/w12/CHANGELOG.md).
- Preserve earlier W12 GNSS indicator/factory-binding repairs and the SS3 relay
  display fix in the integrated history. W12 remains RC-only with no USB timing
  relay; longer negotiated leases and quality-bearing BLE holdover are deferred.

The controller RX refusal was localized and passed bounded recovery checks.
TC-02's brief v69 USB check had no request errors. TC-04 still failed with
persistent USB/CDC nonresponse; v68 also retained controller assertions and MCU
restarts. The new USB lifecycle investigation is a proposal, not an applied fix.
These results do not establish continuous delivery, qualified HOT failover,
metrology readiness or independent UTC accuracy.
