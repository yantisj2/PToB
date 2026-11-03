# Paired Clock Time Transfer from Starlink over Wi-Fi

Jonathan Yantis. Approved for public draft review, 3 November 2026.
See the [public-review approval and GitHub publication record](publication.json).
The reviewed LaTeX/PDF and measured results are unchanged. Firmware and other
unpublished development are excluded from this publication.
The [previous PDF review link](../starlink-ntp-frozen-clock/main.pdf) remains
a byte-identical copy.

**Author-review revision, 3 November 2026 (UTC).** Two fresh SL027 validation
windows retain all 900 future PPS per method. The nominated robust AP hybrid's
prior-phase-corrected RMS is 19.226 and 8.240 microseconds, versus 37.180 and
35.662 microseconds for the AP-free all-NTP/local WLS900 control on the same
witnesses. Each estimator has its own fixed GNSS-measured prior phase base;
there is no future recentering or detrending. R1 improves bias while slightly
worsening centered scatter; R2 improves both. Ordinary AP fits and servos have
mixed outcomes. Raw phase RMS remains above 200 microseconds.

[Read the named paper](starlink-wifi-hybrid-timing.pdf) or edit
[its LaTeX source](starlink-wifi-hybrid-timing.tex).
The paper compares AP-assisted and local-clock estimates using matched
Starlink NTP observations, fixed GNSS-shadow phase calibration and every future
PPS opportunity. Frozen-rate holdover and continuously updated estimates with a
60-second source lease are separate experiments. The original direct-NTP
populations and their poor outcomes remain available below. The earlier beacon feasibility manuscript remains a separate,
unpublished author-review draft and is not needed for public reproduction.

The draft does not establish portable GNSS-free microsecond UTC accuracy.
GNSS supplies the measured phase bases and validation witness, never frequency
training. Fixed bases are client/path/model specific. Native AP unavailability
remains unknown. Software phase continuity, low residual scatter and measured
PPS-input phase agreement do not qualify physical output timing.

## Paired results and public reproduction

The primary protocol is two fresh SL027 windows, R1–R2, each with 900 seconds
of native training and 900 future PPS opportunities. Every continuous filter
parameter and nominee is carried unchanged from the earlier locked matrix;
both fresh runs are validation. No future GNSS error chooses a rate,
filter, horizon or retained tail. Both windows have closed independent
raw/model/map/anchor and method/input/base-history audits: 1800/1800 physical,
local and AP PPS per window, and 900/900 verified future predictions for every
one of the 13 continuous methods, with no new faults, gaps, resets or numerical
mismatches. Refused read attempts remain in the audit, rather than being counted
as missing PPS. One unpaired pre-window NTP record remains an explicit unknown
without entering any retained target or base dependency.
The original three-window plan remains in the evidence. The author requested
a stop after R2 before its future errors were scored; R3 is not acquired under
that amendment and is not counted as a failed or missing third trial.

SL027 separately repairs transactional bridge publication, queued-edge
association and bounded read retry. Refused maps retain the previous committed
model under its unchanged 250 ms lease. Reset barriers and every native-invalid
PPS remain in the semantic input adapter and independent audit. Eight-second
inter-window expiry audits omit complete raw history and are followed by
30 seconds of recorded reprime; no missing history is invented.

The [selected paired evidence](hybrid/README.md) retains five closed windows:
fresh R1–R2 and historical SL025 P1–P3, with all 900 future opportunities per
window and 72,000 method/opportunity rows. P1 was retrospective
development; the matrix, dependencies and nominees were locked before P2/P3
heldout errors. P3 remains a long-memory input/base replay failure. Its
native-computed results and verified conditional subsets stay distinct. The
queued-edge race retains its raw pulses and is not labeled GNSS or USB loss;
a new firmware cohort does not repair old observations.

The separate SL026 600-PPS qualification remains a failed 98.1% gate:
588/600 common availability (98.0%), AP 600/600, zero replay mismatches and
zero new fault-counter increments. The author accepted that 89.54% observed
unavailable-fraction reduction as sufficient to proceed to the distinct SL027
paper cohort. This is not a passed original gate or a new SL027 qualification.

Run from the repository root with Python 3.10 or later:

```sh
python3 papers/starlink-wifi-hybrid-timing/hybrid/analyze.py
python3 tools/verify_starlink_ntp_paper.py
```

The standard-library paired analyzer recomputes phase errors, population
membership, bias, centered scatter, RMS, nearest-rank absolute P99, maximum,
phase drift and explicitly post-hoc detrended precision from the selected
predictions and bases. It does **not** reconstruct NTP acquisition, burst
admission, model publication, the base-witness measurement or raw clock bridges.
Those have separate private causal replay and independent coverage audits,
whose identities and denominators are recorded in the selected provenance.
No ordinary public check requires private records, hardware or credentials.

A complete future record denominator is distinct from complete timing service.
All native-known coordinates and rolling target/input dependencies are audited;
AP-unavailable witnesses are not repaired, dropped from the reliability count,
or made zero-error observations. The primary AP-free baseline is all-NTP/local
WLS900. It is scored on the same AP-common witnesses as the nominated robust AP
model, with its own prior phase base. Huber factors are AP-derived and shared
with local, so the shared-Huber local branch is a secondary AP-assisted weighting
control. Weighting, coordinates and sometimes admitted inputs differ between
the primary systems; their RMS difference does not uniquely isolate an AP
oscillator benefit.

The canonical gate rejects no training minima in either fresh run, so field
spike-rejection benefit is unproven. Observed residual scatter combines source,
path, client endpoints and coordinate effects. Frozen 900-second forecasts
are reported separately: most witnesses exceed the original 60-second source
lease. Low post-hoc detrended scatter is neither their achieved forecast error
nor a calibrated UTC bound. All methods experience the measured logging-load
increase at freeze, leaving physical attribution unresolved.

## Earlier direct-NTP evidence

The original `ntp.csv`, `pps.csv`, `results.json`, `sealed-model.json`,
`observations.json` and `provenance.json`, plus `controls/`, preserve the earlier
SL024 direct-NTP study. Three same-image/boot windows each retain all 600
qualified PPS and all 300 future forecast errors, with frozen RMS 64.215,
7.795 and 18.737 microseconds. The secondary longer-history model shares one
repeat's future witnesses. Earlier incomplete and interrupted populations remain
context; no failed receipt is promoted to a pass.

Recompute these historical selected models with:

```sh
python3 papers/starlink-wifi-hybrid-timing/analyze.py
```

That analyzer reconstructs the historical burst selection, prior OLS gate,
RTT-weighted rate, last-16-PPS base and future errors from its selected NTP/PPS
derivatives. It likewise cannot recreate physical timestamps or calibration
uncertainty. Its original plot helper and generated tables remain historical
artifacts, rather than the current paired-paper figure or current operating
instructions.

## Build and edition identity

Use a public TeX toolchain, for example Tectonic 0.17.0:

```sh
tectonic -X compile papers/starlink-wifi-hybrid-timing/starlink-wifi-hybrid-timing.tex --outdir artifacts/starlink-hybrid-paper-tex
```

`hybrid/tables.tex` and its component tables are generated numerical renderings.
Regenerate the vector scientific plot with public ReportLab installed:

```sh
python3 -m pip install reportlab
python3 papers/starlink-wifi-hybrid-timing/hybrid/plot.py
```

The plot compares AP-free all-NTP WLS900, ordinary AP900 and robust AP900 on the
same AP-common witnesses, breaks unavailable slots and applies no display
smoothing or clipping. Its
vertical scale differs by panel. The step-allowed affine refits are distinct
from the phase-continuous servos in the full comparison matrix.
The [primary-source ledger](source-ledger.json) records inspected passages and
claim limits; references and equations do not import third-party figures or
text. The edition manifest binds the matching canonical source/PDF, tables and
selected inputs after the numerical, build and all-page visual checks. Prior
reviewed bytes remain in Git. A changed source or derived file needs a matching
build, verification, visual review, manifest and changelog;
opening the editor alone is not successful compilation.

The paper, figures and original selected derived datasets are CC BY 4.0,
credited to Jonathan Yantis. Original helper code and ordinary documentation
use MIT; see [rights](LICENSE.md). Private source identities, credentials,
raw laboratory logs, firmware images and private working notes are excluded.
Checksums establish identity, not metrological accuracy. The approved public
review package preserves the reviewed manuscript and PDF bytes, including their
author-review labels. The separate approval record grants public draft review;
it does not promote the paper to a final publication or qualify hardware.
