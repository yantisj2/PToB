# Bench validation for Example 02

The host test and the offline native build check parsing, scope and model
arithmetic, compilation, image inspection and renderer output. Hardware
accuracy, radio duty, sleep current and physical button behavior need fresh
measurements. This directory keeps the bench protocol outside the copyable
receiver. Keep real captures, enrollment and lab logs under ignored local
artifacts.

Use the separately authorized exact W12 and a verified recovery image.
Identify the already serving T096 GM and verify its installed GR3 profile
without disrupting its service. Use the lab's bounded app-only write,
application readback and protected-prefix comparison. Keep its install record
separate from the test data.

Cold-start the receiver without a USB console and watch it reach UTC from the
serving T096. With USB connected, use `status` to observe the source, UTC and
validity. A host UTC reading can be recorded separately for a rough shadow
comparison. Record the host clock's uncertainty and USB observation latency
with that comparison. Use independent PPS for precision scoring.

Keep the receiver running through a six-minute session boundary and verify
that it gets fresh authority from the GM and returns to passive tracking.
Record any renewal outage in the coverage denominator. Where the lab can
isolate the receiver's RF path without changing the GM, verify loss and
reacquisition with USB disconnected. Repeat the cold start and recovery for
each profile before measuring steady-state precision.

Measure against a separate, qualified PPS reference. Record reference and
electrical uncertainty and confirm the actual W12 route before use. For Basic
and Refined, bracket POSIX reads around captured PPS and report that aperture.
For Fine, compare the high-resolution model at independently captured PPS
edges. A physical output has its own endpoint and validation. Where a path
correction is needed, first measure signed clock error in a training window
with independent PPS. The negated median is one simple fixed correction. Bind
it to the receiver, grandmaster and measured setup. Freeze it before a disjoint
validation window and retain the raw and corrected results with their own
denominators. Keep UTC and relative results separate.

For each profile record 600 consecutive PPS opportunities at idle and under
representative app load. Gate on at least 99% valid coverage and the profile's
99th-percentile absolute error target. For Fine, start scoring at the first
PPS after 120 seconds of continuous valid training on one authenticated scope.
Set an acquisition deadline and report a failure to reach that warmup. Keep
cold-start and reacquisition measurements separately. Report signed bias, RMS,
P99, maximum, misses and longest outage. If receiver or witness evidence is
missing, retain the denominator.

Compare with the same app without the receiver. Record incremental flash,
internal RAM, stack high-water, CPU time, acquisition latency and measured
radio-on time where available. Test shorter listen windows only after the
continuous baseline passes. Choose the smallest configuration that still
passes the timing and coverage gates.

Copyright (c) 2026 Jonathan Yantis. [MIT](../../LICENSE).
