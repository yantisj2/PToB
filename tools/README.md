# Reproduction tools

Native firmware builds automatically retain the latest two successful images per
device/profile and release compiler intermediates. Use
`python3 -B tools/firmware_artifacts.py plan --out artifacts/storage-plan.json`
to review cleanup of existing output. See [firmware storage](../docs/firmware-storage.md)
for recovery pins, applying plans and offline verification.

Run `python3 tools/verify_ble_tail_multipath_paper.py` to verify the latest
26 October public review draft's cadence, propagation, channel-mix regression,
holdover, loss-age and confidence examples, distinct W12/N30 populations and
edition identities. Selected reviewed source snapshots make the check independent
of unpublished branch history. See the [multipath paper](../papers/ble-tail-multipath/README.md).

Run `python3 tools/verify_indoor_pnt_paper.py` to verify the 26 October indoor
PNT draft's analytical timing/motion budgets, clock composition, correlation,
geometry, hypothetical pilot/cost examples and separate historical populations.
This standard-library offline analysis operates no hardware and qualifies no
localization, UWB integration or GNSS equivalence.
See the [indoor PNT paper](../papers/indoor-pnt/README.md).

Run `python3 tools/verify_indoor_ips_paper.py` for the 26 October PToB-IPS public
research draft. It checks pseudorange/clock recovery, geometry and covariance,
quantization, correlated averaging and bias floors, aligned multi-rate receiver
reservations and cyclic conflicts, motion arithmetic, selected W12/N30 data and
publication identity. It needs only Python 3.9+ and the public checkout. The
unpublished predecessor is not required. All IPS examples are analytical model
output, not hardware or positioning qualification. See the
[paper and build guide](../papers/indoor-ips/README.md).

Run `python3 tools/verify_s3_fine_paper.py` from the repository root to check
current ESP32-S3 follow-up rows, eligibility, full-window tails, training-derived
offsets, subset identities, denominators and the matching source/PDF manifest.
This standard-library offline verifier does not open hardware or reproduce the
private physical extraction. The [fine-capture collection](../evidence/s3/2026-10-22-fine/README.md)
retains the completed 660-second result and its unqualified output target.

The original `tools/verify_s3_paper.py --require-long-cohort` binds the previous
source/PDF bytes. Run it at the historical revision recorded in the
[follow-up edition receipt](../papers/editions/s3-timing-2026-10-22-fine.json),
`70a9c69ad6f823b8ca1acaf28180c7c010fe748e`, to reproduce that edition. Its
[original manifest](../papers/editions/s3-timing-2026-10-22.json), evidence and
verifier semantics remain unchanged; replacing the working paper does not
reclassify the original incomplete 660-second attempt.

Run `python3 tools/verify_robotics_edition.py` to check the 22 October robotic
gPTP/TSN draft's causal map composition and validity, unknown-bound propagation,
motion, holdover, serialization and simplified schedule examples, candidate
pilot denominator, statistical illustration, separate historical populations,
bibliography and edition identities. The launcher supplies the exact historical S3 citation to the unchanged sealed
verifier, so newer paper editions do not break reproduction. This offline
standard-library check does not implement gPTP, operate devices or establish TSN/robotics qualification.

Run `python3 tools/verify_timecode_paper.py` to check the 22 October Bluetooth
timecode draft's exact rational frame/sample examples, all nominal-day drop-frame
minute transitions and invalid labels, boundary ambiguity, hypothetical drift and
holdover arithmetic, bibliography keys, and edition/source/PDF identity. This
standard-library check does not implement a vendor protocol or qualify hardware
or interoperability. See the [paper catalogue](../papers/README.md).

Run `python3 tools/verify_v14_proposal.py` with Python 3.9+ for the current
30 October author-review source/PDF identities, cited source pins and four
independent coarse TZ wire/HMAC known answers. This does not execute firmware
or qualify failover, RF delivery or accuracy.

Run `python3 tools/verify_v14.py` from the repository root with Python 3.9+ to
also check the immutable V14 import hashes, both separate PPS-phase populations,
complete opportunity accounting, diagnostic deltas and frozen RC1
tables/examples/HMACs. The replaced proposal pair resolves through hash-checked
Git history at the exact revision recorded in the new edition manifest; include
that history when cloning. `tools/check_v14_document.py` checks that frozen
proposal alone. Unchanged imported files are always checked in place.
The [V14 collection](../evidence/v14/2026-10-12/README.md) documents its scope and
optional figure reproduction using `tools/derive_v14_figures.py`.

Run `python3 tools/verify_evidence.py` from the repository root with Python 3.9+
to verify imported evidence hashes and recompute the seven separate PLL capture
error summaries from included CSV samples. It uses only the standard library
and never accesses hardware or private archives. It does not qualify calibration,
physical output, native state decisions, or the original extraction process.

The frozen v12 model has [separate reproduction instructions](../evidence/v12/model/README.md).

Run `python3 tools/verify_research.py` to verify the October 15–16 paper/export
hashes, USB delivery and tracking populations, and hypothetical viability model
outputs. See the [research collection](../evidence/research/2026-10-16/README.md)
for its reproducible subset and excluded private raw captures.

Run `python3 tools/verify_viability_paper.py` to check the 30 October viability
source/PDF edition, two-page expansion limit, pinned public references and
claim/summary correspondence. Add `--replay` to run the supporting public
numerical verifiers, including holdover prefix replay; allow several minutes.
The original imported viability pair is checked through its recorded public
Git revision, with the immutable import hashes preserved. These are document,
data and numerical checks, not physical calibration or firmware acceptance.

Run `python3 tools/verify_stability.py` to check the selected October 16 stability
summary hashes and aggregate accounting. The
[collection](../evidence/stability/2026-10-16/README.md) separates controller
recovery, upstream loss, MCU faults and USB nonresponse. Private raw logs are
excluded; this check does not repeat their physical extraction.
