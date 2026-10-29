# T096 time zone wire test plan

Status: execution authorized by the operator on attached TC-01, TC-02 and TC-04;
TC-03 was added to the authorized cohort on 29 October 2026. The first candidate
was installed on the original three units; acquisition failed and is preserved.
All four units have readback-verified local .205 images, including the standby
TZ correction and requested local clock display. Direct-RC verification passed;
handover qualification and relay availability did not pass. No full wire
certification is claimed.
Copyright (c) 2026 Jonathan Yantis. MIT.

Introduce the [four-byte time zone extension](../time-zones.md) as a breaking
coarse-wire revision on T096 first. Certify its GM and RC behavior on one frozen
candidate before porting other targets. The first live cohort uses manual CDT;
the [regional database and automatic detection](../time-zone-database.md) remain
outside this phase.

## Candidate and bench scope

| Item | Selected test direction |
| --- | --- |
| Units | TC-01, TC-02, TC-03 and TC-04, subject to fresh exact factory/USB identity verification |
| Software | One reviewed source commit and coarse-wire revision across the cohort; per-device build bindings remain distinct |
| GM configuration | `tz_id = 2` (`America/Chicago`), `tz_offset_min = -300`; label CDT |
| Four extension bytes | `02 00 D4 FE`, little endian |
| RC configuration | Inherit the selected authenticated source's pair; no local default masquerading as received TZ |
| Wire migration | New revision only on affected coarse paths; old revisions rejected, no negotiation or downgrade |
| Primary transport | BLE; explicitly disable LoRa fallback for the bench window so it cannot mask BLE loss |
| Page 1 | Known leased CDT changes the label and both analog/digital faces to local time; unknown/expired TZ falls back to UTC |
| Other targets | No W12, N30, SS3, CoreS3-SE or macOS client port/installation in this stage |

Keep compiled per-device enrollment, calibration and radio configuration
preserved. Prefer a separately enrolled bench group, with its inputs and images
kept ignored locally, to exclude old-wire GMs and receivers. Otherwise document
equivalent isolation before hardware execution. A spare legacy GM must not
participate in election, serve time or bridge timing into the candidate cohort.
Do not use an unported instrument as the authoritative new-wire decoder.

Manual CDT stays -300 until explicitly changed. No date-based switch to CST is
implemented or assumed. CST, DST-boundary and conflicting-zone cases initially
use offline fixtures; the live cohort stays CDT.

## Gate 1 Freeze the wire and acceptance criteria offline

Reconcile the candidate against the actual installed T096 source/build receipts
before selecting its base. Guide version numbers, the current checkout and a
receiver codec snapshot are not proof of the installed GM implementation. Do
not regress newer GM/RC behavior merely to fit an older documented baseline.

Before code changes are considered ready for hardware, record:

- Every affected GM publication, direct-GM JOIN/renewal and RC relay/bootstrap
  path; the chosen revision, discriminator, exact lengths, offsets and HMAC
  domains. Preserve existing precision payload bytes and timing semantics.
- How selected-parent authentication supplies the fixed mesh TZ. Its lease is
  three advertising periods, renewed by existing authenticated parent
  advertisements independently of precision lock or map expiry. No precision
  packet extension, additional stream or scan schedule is part of this change.
- The compatibility matrix for old/new GM, old/new RC and mixed relay paths.
  Unsupported coarse revisions cannot supply a new admission, renewal or TZ.
  Unchanged precision traffic cannot bypass this incompatibility fence.
- The 750 ms GM and 300 ms RC coarse TZ leases, normal display refresh interval,
  acquisition budgets and existing timing/availability regression limits. Set these before
  examining candidate results; do not loosen them to fit a failed run.

Create a versioned contract and synthetic vectors separate from frozen
historical contracts. A valid new-format packet with unknown/invalid TZ metadata
falls back to UTC under the wire plan; malformed framing, unsupported revision
and failed authentication are whole-packet rejections. Keep those outcomes
distinct.

## Gate 2 Host and native verification

Exercise production encoders, decoders, owner snapshots, admission/renewal,
relay forwarding and display conversion. A standalone packing example is not
enough. Required cases are:

| Area | Required checks |
| --- | --- |
| Byte contract | Exact CDT bytes, both endians as positive/negative controls, lengths, revision and field bounds |
| Authentication | Mutate TZ bytes, tags, version, group and scope; reject tampering without partial state updates |
| Compatibility | Old/new combinations and an old middle relay cannot silently negotiate or acquire new-format authority |
| Metadata values | UTC, CDT, CST, unknown pair, unknown nonzero ID, positive/fractional-hour offsets, sentinel and range errors |
| Formatting | Date rollover, fall repeated hour and spring gap in synthetic UTC; UTC event identity stays unchanged |
| RC inheritance | Before authentication report unknown; deliberately conflicting local RC defaults cannot replace upstream TZ |
| Coarse lease | Renew from parent advertisements; test just before, at and after three periods; precision-map expiry does not clear TZ |
| Mesh configuration | Same pair through GM/parent changes and renewal; conflicting pair ignored; explicit new configuration/restart clears the cache |
| Relaying | Root to relay to leaf preserves the pair; each hop renews its coarse lease; loss of parent advertisements expires live TZ |
| Timing isolation | For identical synthetic inputs, precision payloads, UTC maps, epochs, source permissions and USB timing are unchanged by TZ |

Use the complete production host suite documented in the
[build guide](build.md), including its provisioning/console checks and relevant
GM/RC renewal, path, handover, USB and renderer regressions. Existing entry points
include `python3 firmware/t096/tools/test.py --deps artifacts/deps`; new TZ tests
and vectors must be integrated into that suite. Do not claim these tests exist
or passed until implementation supplies and executes them.

Build and inspect both GM-boot and RC-boot native variants from the same source,
including the supported runtime GM/RC switch. Validate per-device images,
controller payload/ATT buffer sizes, static memory, stack budgets and normal
scheduling. Preserve existing radio/controller/USB reserves. Do not enable the
database, resize timing buffers speculatively or bundle unrelated fixes.

Gate 2 passes only with complete recorded results and no unresolved blocker
for the frozen candidate. A later code/configuration change invalidates affected
results and requires the corresponding checks again.

## Gate 3 Exact-device installation after operator go ahead

Connection alone does not start testing. When the operator explicitly starts the
session, verify all four identities, USB ownership, installed source/build/boot,
local inputs and recovery records. Aliases are labels, not identity evidence.
Record the isolation arrangement and finite test window before changing roles.

Capture a bounded 600-second pre-update baseline when the installed cohort is
compatible and usable, after its ordinary source qualification. Save actual
roles, topology, availability, renewals, timing statistics and faults. If a
comparable baseline cannot be captured, report it unavailable and resolve the
timing-regression comparison before claiming certification.

Follow the [app-only update guards](build.md) one device at a time: reuse each
identified verified recovery image, preserve the original bounded application
drain and write bounds, read back every new application byte, compare the actual
protected prefix, and preserve outside-application bytes in a partial final
sector when necessary. No full-flash or installed-application backup is part of
this plan. Each unit must reach a fresh source/build/boot-bound CRC status before
proceeding to the next. Failed identity, ownership, recovery or readback stops
the installation sequence.

During mixed-version installation, do not use partial acquisition as a test
result or permit a live old/new timing bridge. Start candidate topology tests
only after all four installations and the isolation checks pass.

## Gate 4 Bounded live topology tests

Role assignments below are intended test configurations, not claims that a unit
will win election. TC-01 and TC-02 must independently qualify for GM service
through the existing GNSS/PPS/dwell rules. Observe the actual elected identity.
Use supported parent selection to prove a relay path; proximity and signal bars
are insufficient. In each chain, the leaf must actually select the middle RC,
with its root and route matching that chain and no direct-GM bypass.

Stable observation durations begin after acquisition. Acquisition gets one
predeclared budget derived from the frozen production rules; expiry is a failed
or incomplete test, not a reason to restart the clock until a good window appears.
Retain startup and failed acquisition records alongside stable observations.

| Run | Intended topology | Minimum observation or repetitions |
| --- | --- | --- |
| D1 | TC-01 GM to TC-02, TC-03 and TC-04 RC directly | 600 seconds |
| D2 | TC-02 GM to TC-01, TC-03 and TC-04 RC directly | 600 seconds |
| R1 | TC-01 GM to TC-02 RC relay to TC-04 RC leaf; TC-03 follows TC-01 directly | 600 seconds |
| R2 | TC-02 GM to TC-01 RC relay to TC-04 RC leaf; TC-03 follows TC-02 directly | 600 seconds |
| H1 | TC-01 and TC-02 GM candidates, TC-03 and TC-04 RC | Three controlled serving-root loss/return cycles, observing actual identity changes |
| B1 | Repeat a chain with the selected relay rebooted, then the serving GM rebooted | One of each; verify fresh boot/scope and reacquisition |
| S1 | One fixed proved relay chain | 1,800 seconds, with at least three observed successful renewals on each applicable direct-GM and relay admission path |

This covers GM on TC-01/TC-02 and RC on all four units, with two BLE links and
one intermediate RC. The original matrix added TC-03 as a direct RC; the
source-availability adjustment below extends its GM and relay tests. TC-04 GM
and larger mesh capacity remain outside this scope.

For H1, establish the ordinarily eligible ACTIVE/HOT relationship where the
candidate supports it, including both sides' acknowledged current state. Use
only the reviewed source/role withdrawal or operator shutdown method; unplugging
USB from a battery-powered GM is not proof that its RF source stopped. Record
each actual root change, outage and reacquisition. Returning the old GM must retain the same mesh TZ and must not cause a second
serving root. Timing scope rules remain unchanged. A missing
eligible standby leaves this test incomplete; do not bypass its qualification.
These finite cycles test TZ behavior during GM replacement, not seamless or
generally durable failover.

During R1/R2 or B1, retain a bounded source-loss interval exceeding three
advertising periods without authenticated parent broadcasts. Each receiver whose immediate parent
broadcasts disappear must report unknown/UTC and cannot fall back to its compiled
CDT default. A descendant that still receives authenticated parent presence may
retain the fixed mesh TZ even while upstream precision is unavailable. Renewed service must supply a
fresh matching pair. Production timing/holdover rules remain independently in
force. Keep fault injection and outages separate from stable S1 statistics.

## Observations and pass criteria

Use one owned, finite collector per serial device, with source/build/boot/CRC
binding and original observation timestamps. Extend the existing
[console capture](diagnostics.md#console-capture) as needed before hardware work;
the present collector is not assumed to expose unimplemented TZ fields. Capture:

- Active coarse revision, selected root, immediate parent, route, received and
  transmitted TZ pair, and the three-period lease boundary. Keep precision
  diagnostics separate from coarse TZ validity.
- Selected UTC and separately formatted local time, precision/map/source state,
  renewals, radio/controller faults, watchdog events, drops and missing samples.
- Actual RC transition from unknown to received CDT after authenticated coarse
  acquisition, joined to selected-parent/root observations and the exact build.
  Compare the frozen production codec vectors separately. Constant local CDT
  defaults cannot prove reception. An RF packet capture, if available, is
  additional wire evidence rather than a requirement for coarse metadata tests.

For every eligible fresh observation, the exact pair must be `(2, -300)` and
local time must equal its sampled UTC minus 18,000 seconds, including date
rollover. A GM or parent switch within the same mesh preserves its configured pair;
matching authenticated advertisements renew the coarse lease. Precision expiry
must not clear it. Actual parent-broadcast loss expires live TZ after three
periods, and receipt of matching advertisements restores it. Check presentation
at the next normal refresh; retain ordinary RF misses in the results.

Check RC USB timing with a bounded owned capture and offline decoder: exported
time remains UTC, not five hours earlier. Normal GM's lack of an RC USB source
map remains expected behavior. No macOS chrony launch or OS clock operation is
needed for this check. Preserve timing precision/regression limits established
before the run; console timestamps and internal RMS do not establish absolute
UTC or physical PPS/output accuracy.

Require zero incorrect accepted TZ pairs, authentication/replay/scope escapes,
unexpected resets or new runtime faults in required runs. Complete the stated
durations and actual renewal counts; elapsed time alone does not prove renewal.
Ordinary RF losses, serial CRC errors, missing intervals and unscorable timing
records remain in their denominators. Insufficient evidence is INCOMPLETE, not
PASS. Report each run as PASS, FAIL, INCOMPLETE or NOT RUN with its reason.

## Certification and later target rollout

The deliverable is a scoped **T096 TZ wire GM/RC certificate** tied to source
commit, per-unit image hashes, new wire revision, exact tested identities and
boots, manual CDT setting, tested topologies, all required results and known
limits. Preserve raw/enrolled material under ignored local artifacts and publish
only separately reviewed sanitized results. It certifies the new TZ behavior
and measured regression scope, not new absolute timing or failover accuracy.

Only after all required gates pass:

1. Freeze the T096 codec, contract and vectors as the reference for ports.
2. Port W12/N30 through their shared receiver/relay code with explicit snapshot
   provenance and target-native tests, then add one updated receiver to the
   proved T096 GM. Verify each target directly before its supported relay path.
   W12 BLE50 stays client-only; estimated relays retain their existing limits.
3. Port SS3 and CoreS3-SE's actual consumed coarse/control decoders and display
   metadata, preserving each target's source-selection and expiry rules. Use
   the same vectors, then separate direct/relay or instrument checks as supported.
4. Run macOS offline UTC/UTS1 regressions. Port software only where it consumes
   a changed wire or needs a requested TZ display; the UTC USB time base stays
   unchanged. No GM support is introduced on receiver-only targets.

LoRa TZ transport remains outside the certificate; fallback must not label its
UTC with stale BLE TZ. Automatic zone detection and calendar-based DST remain
separate later phases. A T096 pass alone never certifies another target.

## Execution checkpoint

The initial .202 candidate passed host tests and the existing native inspection,
but its bounded live acquisition failed before a stable observation window.
Application and controller translation units selected different coarse wire sizes
for a shared job structure. Preserve this failure separately from later runs.
The .203 correction applies the same wire selection to the application and both
controller libraries. Native inspection now checks the actual compiler commands;
the failed .202 commands provide a negative control. Neither the correction nor
TC-03's inclusion relaxes the original acquisition or availability criteria.

All four .203 installations passed complete application readback and protected
region/tail comparison. The first .203 D1 acquisition then exhausted its original
240-second budget: TC-01 lacked a GNSS fix/PPS and could not qualify as GM. Its
input subsequently returned intermittently. Preserve that failure as a separate
hardware/source-availability result; it is not a passing GM test for TC-01.

The operator-added TC-03 has live GNSS/PPS. The execution matrix therefore uses
TC-02 and TC-03 for the two GM rotations, retaining the same source qualification,
acquisition budget, durations, availability threshold and failure rules:

| Run | Execution topology | Required observation |
| --- | --- | --- |
| D2 | TC-02 GM; TC-01/03/04 direct RCs | 600 seconds |
| D3 | TC-03 GM; TC-01/02/04 direct RCs | 600 seconds |
| R2 | TC-02 GM → TC-01 relay → TC-04 leaf; TC-03 direct | 600 seconds |
| R3 | TC-03 GM → TC-02 relay → TC-04 leaf; TC-01 direct | 600 seconds |
| H1 | TC-02/03 eligible GM pair; TC-01/04 RCs | Three actual supported YIELD cycles |
| B1 | TC-02 GM, TC-01 relay, TC-04 leaf, TC-03 direct | Guarded relay and root reboot/return, actual fresh boots and reacquisition |
| S3 | TC-02 GM → TC-03 relay → TC-04 leaf; TC-01 direct | 1,800 seconds and at least three actual renewals per applicable path |

Any resulting certificate must identify TC-02/03 as the tested GMs and retain
TC-01's failed GM attempt. It must not promote TC-01 or TC-04 to serving-GM
acceptance. The TZ format and three-advertising-period lease are unchanged.

The initial D2 stable collector stopped on an ambiguous USB ownership result;
all handles closed. Its incomplete window is retained. Collector subprocess
inventory scans are now serialized inside the original eight-second budget,
with both serial aliases checked and every foreign PID still rejected. A fresh
full observation window is required; do not splice its samples into the failed
capture.

The .203 direct and relay rotations completed their 600-second observation
windows. Its H1 precondition failed before any YIELD: the standby GM's coarse
TZ remained unknown despite ordinary HOT admission. The receipt path had used
a future precision-deadline converter for a past receive timestamp. The .204
correction starts the same three-period coarse lease from kernel uptime after
an accepted selected-root announcement. A production-body regression rejects
the old behavior. Preserve the .203 results; they do not certify its successor.

The operator subsequently requested a local-time page 1. Candidate .205 carries
that presentation change forward: exact renderer checks compare both clock
faces and the three-letter label against the expected local time, cover CDT,
CST, date rollover and unknown/expired UTC fallback, and retain UTC in the
underlying model and other diagnostic pages. Its screensaver follows the same
presentation. Repeat the required live matrix on one frozen successor across
all four units; do not combine earlier source versions into its certificate.

### Local .205 results, 29 October 2026

The frozen native-development candidate is commit
`13bfcfadc1e0122f9d9e1bcc9bf6413afa7427a1`. All four app-only installations
passed actual application readback and protected-region/tail comparison. The
complete production-body host suite and all four native inspections passed.
Renderer checks cover both faces, CDT/CST labels, date rollover, the saver,
known TZ while acquiring, unknown/expired UTC fallback and LoRa UTC fallback.
Primary status and USB timestamps remain UTC.

Two complete 600-second observations produced the following known-CDT status
populations. Unknown samples remain in their denominators; neither window
accepted an incorrect TZ pair.

| Run | TC-01 | TC-02 | TC-03 | TC-04 | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| D3, TC-03 GM and three direct RCs | 117/117 | 117/117 | 118/118 | 117/117 | PASS |
| R3, TC-03 → TC-02 → TC-04, TC-01 direct | 72/117 | 76/117 | 117/117 | 77/117 | FAIL, below 99% availability |

Every sampled direct RC map in D3 was valid and locked (116/116 per RC).
During R3, TC-03 served in 76/116 network samples; the receivers withdrew their
maps and live TZ during the interruption, then recovered. Its PPS-rejection
counter increased from 1 to 15 and its reported source epoch changed. These
are diagnostic observations; the cause of the serving interruption is unresolved.
All R3 collectors completed and closed, with no reported runtime or transport
error. A good tail does not replace the failed whole window.

The first H1 attempt exhausted its original 240-second source-readiness budget
before YIELD: TC-02 had no GNSS fix or PPS. Its fix/PPS subsequently returned,
but qualification was not sustained. A later request was queued asynchronously
and then rejected as stale; no completed handover is counted. That attempt also
exposed a harness error: the pinned embedded SDK defines `EINPROGRESS` as 119,
where the test had expected Linux's 115. Preserve that capture. A fresh harness
using the target value again exhausted the original readiness budget before
YIELD. The three required verified cycles remain incomplete. At the end of the
first H1 capture, all four units reported received/configured CDT, valid local
conversion and active analog faces despite TC-02's local-source failure.

An additional exact-TC-04 USB check under the qualified D3 root returned
600/600 fresh valid UTC records with matching device, physical boot and root
mesh-session identities. This confirms the display offset did not shift those
UTC records. The overall probe failed with one SOF request failure, also seen
on .203. Preserve that failure; USB durability and calibrated UTC accuracy
remain unqualified.

D2, R2, B1 and the 1,800-second S3 gate have not completed on .205. The earlier
.203 successes do not fill those gaps. Enrolled images and raw captures remain
local private artifacts. Other target ports remain deferred pending the required
GM/RC wire certification and a stable source test population.

## Failure and closure

Stop on an identity/readback failure, wire/authentication escape, TZ-caused UTC
step, source/expiry violation or unexpected runtime fault. Retain the failed run
and resolve it before resuming affected gates. Repeating only a good tail cannot
replace the original failure record.

Prepare a whole-cohort rollback using the already verified own-device recovery
images and saved inputs. Rollback execution requires its reviewed operator
scope; no mass erase or full-flash backup is implied. Keep old/new cohorts
isolated during either direction of the change.

At closure, leave the agreed tested roles/isolation or the explicitly restored
cohort state, close all owned descriptors, verify ownership release and stop
finite collectors. No background watcher or daemon remains. Plugging the units
in later is a readiness step; execution starts only on the operator's go ahead.
