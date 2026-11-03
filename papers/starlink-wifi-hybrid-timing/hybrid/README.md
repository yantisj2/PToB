# Selected paired-clock phase observations

Jonathan Yantis. Original selected data and generated tables: CC BY 4.0.
Original analysis code: MIT. This is an author-review dataset, not a calibrated
UTC reference or a qualified time-service result.

The primary cohort is SL027 R1/R2. Both use candidates and parameters locked
using historical SL025 P1, before its P2/P3 validation errors were read. The
original three-window SL027 sequence was shortened to two by author instruction
after R1 was available and before R2's future window. R3 was not acquired; it
is not a failed or missing trial. Both acquired windows remain in the study.
Historical P1/P2/P3, including P3's incomplete replay dependencies, remain in
the same selected dataset as a separate firmware cohort.

Each closed window has 900 training seconds and 900 future physical PPS
opportunities. Image, boot and request cadence remain unchanged within each
cohort. The 72,000 selected rows retain every future opportunity for all 13
continuous candidates and three frozen controls across the five windows.
Frozen estimates and continuously updated candidates are separate experiments
on those observations. The original MIN8 runtime clock remains a separate
control.

`future.csv` explicitly selects every future opportunity for each declared
candidate and frozen control. It contains relative local coordinates, signed
raw predicted PPS phase, fixed-base errors, true source age and qualification
flags. Unavailable values are empty, never zero. `pps` is an ordinal relative
to the first future pulse; it does not expose a factory identity or UTC epoch.
An unavailable local projection has no invented relative time. Frozen source
ages of 60 seconds or more are research holdover. Continuous qualified results
retain the original 60-second lease.

`raw80_relative_ticks` retains the physical capture counter relative to the
first future pulse, at its nominal 80 MHz rate. A post-hoc diagnostic compares
this coordinate with nominal one-second PPS ordinal intervals on the same
AP-common witnesses. It also compares the mapped local coordinate with that
counter. These trends never train a correction and do not calibrate the
reference or establish UTC-second labels.

`bases.json` retains each estimator's own fixed prior GNSS-shadow phase base,
the common calibration identities relative to the first future pulse, and its
sample count. Native frozen numeric bases were prospectively sealed. The
continuous methods use a prior-only calibration rule locked before heldout
inspection; their numeric bases were computed offline, without future inputs.
These are external phase calibrations, not GNSS frequency training.
The primary system comparison is phase-corrected RMS for the nominated robust
AP hybrid (`Huber_AP900`) versus AP-free NTP WLS (`all900`) on identical scored
PPS identities. Each method uses its own prior phase base, measured on the same
prior PPS identity set. The comparison changes weighting and coordinate
together and does not isolate the AP oscillator's contribution. The matched
Huber local branch shares AP-derived weights, so it is an AP-assisted secondary
control. The ordinary AP WLS and every other declared candidate remain in the
tables/data, including worse outcomes. Raw phase statistics remain separate.

`admitted` denotes an available native-qualified prediction. `raw_verified`
requires the relevant independently reconstructed maps and input history;
`base_verified` separately audits the prior calibration dependencies.
`verified` requires both. An unknown early input can expire from a rolling
prediction while remaining in its fixed prior base, so a raw prediction and
its calibrated error can have different verification populations.
`native_local`, `native_AP`, `deep_local` and `deep_AP` preserve distinct
availability and replay populations. A local estimate with AP-derived training
weights does not require an AP projection at the scored pulse; paired
comparisons still use identical eligible pulse identities.

Run the standard-library reproduction from any checkout, without devices,
credentials or private logs:

```sh
python3 papers/starlink-wifi-hybrid-timing/hybrid/analyze.py
python3 papers/starlink-wifi-hybrid-timing/hybrid/analyze.py --write
```

The first command prints the recomputed results. The second regenerates
`results.json` and the numerical TeX tables. It checks the 900-opportunity
denominators, phase-base subtraction, source lease and explicit unknowns,
then recomputes raw and calibrated signed mean, centered population scatter,
RMS, nearest-rank absolute P99, maximum, error slope and descriptive post-hoc
detrended RMS. The primary comparison also decomposes the change in mean
squared error into squared-mean and variance contributions; lower RMS need
not mean lower centered jitter. Future means and fitted slopes never correct the reported
forecast errors. The full declared matrix remains, including poor methods;
validation results do not select a new winner.

The vector figure uses public ReportLab (`pip install reportlab`):

```sh
python3 papers/starlink-wifi-hybrid-timing/hybrid/plot.py
```

Its NTP-only WLS, ordinary AP WLS and robust AP curves use identical AP-common
witnesses, break every missing interval and retain all error tails without
smoothing. Each panel has its own vertical scale. Its shared-AP Huber family
has a separate local control in the numerical matrix. This plot does not
show the phase-continuous servo candidates.

An empty verified comparison is shown as an empty panel. All native-computed
observations remain in the data and separate JSON statistics. A dagger on a
table entry identifies a native-computed population whose input or fixed-base
history is incompletely verified; it is not a clean replication or a filter
selection result. The strict verified populations remain separate in JSON.

These selected derivatives reproduce subtraction and statistics from retained
predictions and fixed prior bases. They do **not** reproduce the private causal
estimator execution, original phase-base measurements, software packet
endpoints, physical counter bridges or raw serial capture. The separate local
audits reconstructed those histories; their identities, counts, conditions and
limits are in `provenance.json`. Hashes establish identity, not accuracy.
The selection excludes credentials, network/factory identities, absolute UTC,
GNSS location, raw packet streams and firmware images.

Observed NTP residual scatter combines the source, network path, client
timestamps and coordinate projection. It is not isolated source jitter.
Training-to-future logging workload changes and client bridge resets remain
confounds; an AP bridge reset is not evidence of an AP time correction.
GNSS pulse association, RF reference points, fixed receiver/GPIO delay,
physical clock output and traceable UTC uncertainty remain unqualified.
