# Reproduction tools

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

Run `python3 tools/verify_robotics_paper.py` to check the 22 October robotic
gPTP/TSN draft's causal map composition and validity, unknown-bound propagation,
motion, holdover, serialization and simplified schedule examples, candidate
pilot denominator, statistical illustration, separate historical populations,
bibliography and edition identities. This offline standard-library check does
not implement gPTP, operate devices or establish TSN/robotics qualification.

Run `python3 tools/verify_timecode_paper.py` to check the 22 October Bluetooth
timecode draft's exact rational frame/sample examples, all nominal-day drop-frame
minute transitions and invalid labels, boundary ambiguity, hypothetical drift and
holdover arithmetic, bibliography keys, and edition/source/PDF identity. This
standard-library check does not implement a vendor protocol or qualify hardware
or interoperability. See the [paper catalogue](../papers/README.md).

Run `python3 tools/verify_v14.py` from the repository root with Python 3.9+ to
check the V14 import hashes, both separate PPS-phase populations, complete
opportunity accounting, diagnostic deltas, and proposal tables/examples/HMACs.
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

Run `python3 tools/verify_stability.py` to check the selected October 16 stability
summary hashes and aggregate accounting. The
[collection](../evidence/stability/2026-10-16/README.md) separates controller
recovery, upstream loss, MCU faults and USB nonresponse. Private raw logs are
excluded; this check does not repeat their physical extraction.
