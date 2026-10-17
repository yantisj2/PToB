# Timing-chain stability results — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. Original evidence summary and selected
results: [CC BY 4.0](../../LICENSE.md). Verification code: MIT.

This is a curated engineering record for T096 RC7, W12 native G1/RC7 and the
macOS USB client. It covers local observations on 16 October CDT, including
captures dated 17 October UTC. It is separate from the historical research
collections and does not revise their claims or paper editions.

The source update contains T096 0.15.69, W12 0.1.7 and macOS continuity/recovery
changes. **TC-04's persistent USB failure remains unresolved.** A retained local
clock can advance during an outage while actual delivery has stopped; it is
never counted as a received timing measurement.

## Implemented fixes and bounded observations

| Change / population | Observed result | Limit |
| --- | --- | --- |
| T096 v61 native initiation refusal | 215 eligible GM advertisements, 215 stock RX allocator refusals, zero prepared requests | Localized before request preparation; no independent RF observation |
| T096 v63 shared RX pool 5 → 12 | Baseline plus 19 actual owner-clear recoveries; 37 prepared requests and 37 completed native attempts; zero RX/window refusals | Separate authenticated decisions: 19 accepted, 6 coarse-time refusals; bounded recovery only |
| T096 v63 TC-02 automatic startup | 30/30 locked observations before the operator moved it to the field | Optional relay and later five-minute check unavailable after removal |
| T096 v68/v69 continuity policy | Retained local estimate separated from authenticated session/root permission; original expiry and downstream gates retained | Firmware and host projection have unknown error; no synthetic chrony feed |
| T096 v69 USB map access | Projection/division/serialization moved outside the map lock; production-body race/sanitizer cases passed | Does not prove radio faults or the USB blackout repaired |
| W12 v0.1.7 C-4 observation | 360.010 s, 78 status frames: 71 FOLLOW and 7 valid HOLD; no observed fault/reset; minimum main-stack watermark 3,928 bytes | Intermittent reception remained; status snapshots do not establish continuous RF service |
| W12 v0.1.7 C-1/C-2 startup | One application write and return each, built-in write MD5 and two fresh frames each; minimum stack watermark 4,000 bytes | No independent postwrite readback or extended observation in these quick updates |

The initial W12 0.1.6 candidate passed build/host checks but overflowed task main
on its startup status request. It was withdrawn and a healthy predecessor was
restored before 0.1.7. The successor keeps the 12,288-byte main stack and separates
large snapshots. Its selected static path is 6,800 bytes, with a required
4,096-byte reserve for uncounted callees. This is a selected-path gate, not a
complete call-graph proof. The historical `main_stack_free_words` status field
is byte-valued on the pinned SDK. An attempted C-4 diagnostic capture was refused;
the corrected retry was cancelled by an operator device swap and never ran.

## Failures retained as separate populations

| Population | Actual delivered records / failure | Interpretation |
| --- | --- | --- |
| TC-04 v67, USB64, 900.038 s | 57,599 sample events, 57,598 precision replies, 48,194 valid; one request error/missing packet | A 132.765 s explicit-invalid window recovered under the same TC-02 root boot/epoch while USB kept answering. Later operator return to TC-01 had a separate 10.359 s invalid window. This was upstream/source reacquisition loss, not persistent EP0 nonresponse. |
| TC-04 v68 v4, USB64, 363.730 s process exposure | Saved JSONL spans 363.310 s and contains three MCU boot transitions; all three have source-bound, CRC-valid retained controller assertions | Failed/interrupted test. A fourth matching assertion precedes this population. Reopening after reboot does not prove recovery of the old USB session. |
| TC-04 v69, USB64, 60.036 s | 525 sample events, 514 precision replies: 504 valid and 10 explicitly invalid. First failure around 8.059 s; ten new helpers then failed on anchor OUT | Persistent USB/CDC failure. All eleven transport ends reported the same host registry identity; no fresh MCU boot attestation after the blackout. |
| Simultaneous TC-02 v69, USB64, 60.038 s | 3,839 precision replies: 3,748 valid and 91 explicitly invalid; zero request errors | Brief transport functionality only, with unavailable precision retained in the denominator |

T096 v68 had actual application/protected-region readback and supported return
verification. The v69 quick updates used one application copy per unit and fresh
runtime attestation; extra independent new-image readbacks and identical-image
return cycles were explicitly skipped. Do not transfer v68 readback proof to v69.

Earlier failed v65/v66 trials, other v67 gaps, and the interrupted 4,703.767 s
USB32 chrony run remain private evidence. They are not pooled with this table.
The historical chrony run's 52 daemon starts belong to the older host policy;
they do not characterize the current user session or the new continuity policy.

## What the GM and USB review establishes

Across TC-04's first v69 blackout, 30-second bracketing TC-01 frames show continued
GPS/PPS, prepared publication and accepted TX-capture progress. TC-02 and TC-03
also continued accepting corrections. TC-04's last delivered reply had about
31 ms of receiver discipline age. These observations oppose a sustained shared
GM shutdown but cannot exclude shorter stalls or prove every RF opportunity.

Later GM events strongly support authenticated TC-04 JOIN activity after USB
loss. They do not establish the same TC-04 MCU boot, successful admission or
precision delivery. The four v68 retained faults instead map to stock
`!radio_is_ready()` assertions in `lll_adv.c` and `lll_adv_aux.c`; assertions
remain enabled and their initiating cause is unproven.

Offline tests of exact pinned driver bodies reproduce a latent reinitialization
defect: after queue exhaustion, completed USB reinitialization can omit replay of
already-consumed VBUS/power state and leave USB disabled with stale software
flags. Its expected D+ removal weakens it as the explanation for the observed
same-registry v69 failure. A DMA/worker wait before detach fits that signature
but was not observed on hardware. No lifecycle repair has been implemented.

The next proposal is bounded USB worker/DMA/EP0 progress diagnostics, followed by
branch-specific recovery with one owner, generation fences, finite waits and
correct power-state replay. RF/source loss needs separate arm/completion,
decode/match and rejected-root diagnostics. Keep RF16; normal USB32 is already a
load mitigation and is not a verified cure. The offline investigation made no
hardware or production-source changes.

A separate source/ELF finding applies to installed GM57: its normal GM backend
has no reachable USB source-map publisher, although the USB interface is present.
It can answer counters or explicit source-unavailable records while serving RF.
Use a compatible T096 RC for the documented macOS chain; this capability gap is
distinct from RC69's nonresponse. No current user-selected serial was attested
by the offline review.

## Integrity, exclusions and reproduction

[results.json](results.json) contains the selected numerical populations and
original development source revisions. The [manifest](manifest.json) records
their public hashes, selected private input hashes, transformations and exclusions.
The source branches are integrated into the public history; hardware build
configuration/enrollment and images remain excluded.

From the repository root:

```sh
python3 tools/verify_stability.py
```

This checks the public file hashes and arithmetic/accounting of the included
summary. It cannot re-extract physical observations without the excluded raw
captures. Production-body tests are documented in the target build guides;
their fake hardware boundaries do not qualify physical performance.

The private review used fixed complete-line console prefixes with byte lengths
and hashes. Its event audit retained all core variants by unit/source/MCU boot/
sequence: 96,784 console rows, 18,992 timing candidates, 13,554 bound observations,
6,588 unique event keys, zero conflicting variants; 5,438 no-event polls excluded.
That does not fill initial truncation, missing in-run events, logger ownership
scan timeouts, USB disconnects or unknown intervals. Incomplete closures remain
incomplete even when file-descriptor closure was separately proved. No lossless
continuous soak is claimed. The seven-day logging campaign was still incomplete
at this evidence cutoff.

Real silicon/boot identifiers, enrollment, enrolled images, private paths,
process ownership details and raw USB/serial/RF captures are excluded. Aliases
identify only the historical cohorts. Source checksums establish identity, not
accuracy. TC-03 was intentionally in RC mode; HOT redundancy, independently
calibrated UTC/output accuracy and metrology readiness remain unqualified.
