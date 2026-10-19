# T096 Power Control (PC): awake-only experiment

Copyright (c) 2026 Jonathan Yantis. MIT.

PC is a separate flash-only enrolled BLE leaf image. It cannot select GM, RC or
DC at runtime. Its direct GM is supplied explicitly at build time. It links no
normal GM service owner, GM election or downstream timing owner. GM/RC/DC retain
their owners, timing configuration and 500 ms map expiry. PC reuses the pinned
controller/capture/USB patches and awake synthesized-LF clock configuration of
the mesh build. It implements no sleep or power measurement, and has no measured
RF-airtime counter.

## Build and inspect

Use the [build guide](build.md) and ignored exact-device inputs:

```sh
artifacts/deps/venv/bin/python firmware/t096/tools/build.py \
  --deps artifacts/deps --out artifacts/build-pc-01 --profile PC \
  --pc-parent <16-lowercase-hex-direct-GM-identity> \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/device.json
artifacts/deps/venv/bin/python firmware/t096/tools/inspect_build.py artifacts/build-pc-01
CC=clang python3 firmware/t096/tools/pc_tests.py --out artifacts/check-pc-01
```

The local PC image contains the group key and exact device binding; keep the
entire output ignored. Installation requires exact-device ownership, verified
recovery, bounded maintenance, app-only writes, actual application readback and
protected-region comparison. The builder never opens hardware.

## Treatment and observations

Boot starts continuous-reception BASELINE. The bound command `pc-run MODE
DURATIONS` starts a finite treatment. MODE is eight hexadecimal digits: 1 warm
shadow, 2 cold-estimator shadow, 3 warm radio, 4 cold-estimator radio. DURATIONS
is sixteen hexadecimal digits: upper 32 bits listening milliseconds, lower 32
bits HOLD milliseconds. Listening is 1–120000 ms; HOLD is 1–300000 ms. Concurrent
treatments are refused. `pc CURSOR` reads one causal BLE record after an
eight-hex-digit cursor, with treatment status. Standard silicon/source/boot and
request binding applies.

Phases: 0 BASELINE, 1 ACQUIRE, 2 LISTEN, 3 HOLD, 4 DONE, 5 FAILED. ACQUIRE has a
60 s ceiling. LISTEN begins at the first completed timing pair, independently
of advertisements, valid fit or RMS. HOLD freezes a fresh already published
map. Its pairs cannot train the rate fit or delivered map. Cold-estimator
treatments clear training at the receiver-worker boundary, retaining the
precision counter, GNSS witness and any live authenticated admission. They are
not cold physical boots or deep-sleep experiments.

Shadow HOLD keeps receiving. Radio HOLD requests the existing complete bounded
receiver drain; `rf_closed_tick` records completion rather than the request. On
expiry the receiver restarts through configure/run with a new incarnation. This
resets the estimator, so even a warm radio treatment has cold training on return.
Restart/authentication latency is a separate cost. Neither mode establishes
measured energy savings. Radio-off intervals are inferred from owner closure,
not an independent RF instrument.

The frozen experimental projection can outlive normal map permission. Normal
USB timing maps still expire; projection renews no source, session, authentication
or output permission. The first returning BLE pair is scored against the frozen
map before training. Later records use the preceding published map. An ancestry
or capture mismatch is unavailable. The 32-record ring exposes produced and
overwritten counters; retain cursor gaps. GNSS PPS is observation-only, never
BLE fit input. Core watchdog and maintenance remain; planned HOLD does not arm
the ordinary failed-parent recovery policy.

`shadow CURSOR` retains raw PPS observations and reference metadata. During
HOLD, `pc_projection=true` identifies a frozen experimental map; `utc_ns=0` is
unavailable and only fractional relative phase is evaluated. That record's
`map_valid` denotes arithmetic projection, not timing authority. Witness
collection is independent of UI pages; the first page identifies PC AWAKE.

## Sparse refresh experiment

Version 0.15.113 adds `pc-cycle CYCLES DURATIONS`, with eight hexadecimal digits
for 1–128 cycles and the same packed listening/HOLD milliseconds. Bursts are
1–2000 ms, HOLD is 1–60000 ms and the complete run is at most 900 s. Start from
a fresh map after an ordinary acquisition; no estimator reset occurs between
bursts. Mode 5 identifies this separate, awake shadow experiment.

Burst boundaries follow the original hardware-counter schedule. Missing pairs
cannot extend a burst. HOLD projects the last map while suppressing estimator
training. A burst with no pairs or no new map increments `missed_bursts` and
retains the previous projection; its missing observations remain unknown.
Finite completion keeps training suppressed until the next experiment command.
Each record identifies its cycle and whether it was admitted for training.
The retained 16-pair estimator can span several sparse bursts.

Reception remains enabled to obtain causal error observations between bursts.
The selected pair count and scheduled training fraction are experimental inputs;
neither is measured RF airtime or energy. Radio-off reacquisition and physical
sleep retain separate populations. An incomplete burst/run, unavailable causal
score, missing PPS, and invalid normal output permission remain explicit.

## Evidence protocol

Start with a ten-minute baseline, then 1, 2, 4, 8, 16, 32 and 64 s listening
budgets with 60 s HOLD, five repetitions in recorded randomized order. Screen
cold-estimator shadow treatments first, then selected radio-HOLD cases. Refine
boundaries and extend promising cases to 120/300 s separately. Continuous clock,
logical estimator reset, physical reset and future sleep are separate populations.

Record source/commit/image identity, physical boot, receiver incarnation, parent/
root identities and epochs, map generations, actual command and transition
times, accepted pairs, witness flags, temperature, logger gaps and failures.
Raw logs and enrolled images remain ignored locally. Seal closed runs with
SHA-256 manifests; retain protocol, analyzer version and commands. Original
evidence records and derived CSVs use CC BY 4.0; analysis code uses MIT, credit
Jonathan Yantis. Public evidence requires selected, reviewed, sanitized exports.

At each fixed HOLD-age bin, report phase RMS including bias, signed mean,
standard deviation, maximum absolute error, denominators and missing/invalid
observations across repetitions. Keep startup transients and unsuccessful runs.
Do not tune away bias or qualify startup using error magnitude. Sub-1/10/100 us
are experimental RMS targets, not worst-case promises. A 1 PPS witness cannot
resolve subsecond HOLD trajectories. Unknown PPS/UTC association, receiver and
RF timestamp bias prevent calibrated UTC/output claims.

`tools/pc_analyze.py` analyzes normalized observation JSONL offline, retaining
separate root/parent/physical-boot/capture-epoch populations. Empty HOLD-age
windows retain unknown RMS. Explicit capture boundaries exclude pre-window ring
contents and earlier canary metadata. `pc_tests.py` also checks this accounting,
including bias-preserving RMS, missing windows and epoch separation. Raw capture
and normalized input hashes must accompany any derived results.
