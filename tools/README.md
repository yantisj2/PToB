# Reproduction tools

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
