# October 15–16 research and exported results

These selected records accompany the USB/macOS chrony paper, White Rabbit study
and system viability synthesis. Read the [papers index](../../../papers/README.md)
for the source/PDF editions. Original records and derived datasets are CC BY 4.0,
Jonathan Yantis; the original model and verification scripts are MIT.

| Collection | Included | Boundary |
| --- | --- | --- |
| USB/chrony, October 15 | Delivery and tracking CSVs, original derivation manifest, sanitized evidence summary, literature/build records | Relative agreement against a shared source, not independent UTC accuracy |
| White Rabbit study, October 15 | Claim/input manifest, analytical examples, literature and build records | Architecture and proposed oscillator extension; no new hardware or deployed actuator |
| Viability synthesis, October 16 | Claim/input manifest, literature/build records, model script and four output tables | Separate experiments remain separate; scaling/holdover values are hypothetical |

## Reproduce the included subset

From the repository root:

```sh
python3 tools/verify_research.py
```

The verifier checks all 23 selected import hashes, recomputes the USB delivery
populations and RMS, checks tracking populations/startup, and regenerates the
viability model outputs in a temporary directory. It uses Python's standard
library and Git to obtain superseded paper bytes from preserved public history;
it does not read private captures or operate hardware.

| USB/chrony population | Denominator | Valid / withheld | Result |
| --- | --- | --- | --- |
| Full delivery, 0–300 s | 296 status observations | 220 / 76 | 399.373475 ns RMS |
| Predeclared settled delivery, 60–300 s | 236 status observations | 182 / 54 | 414.351008 ns RMS |
| Settled tracking queries | 265 | Separate chrony estimator population | 23–339 ns reported RMS; median 129 ns |

The full tracking CSV retains the 32.072201 ms startup transient. Empty delivery
cells are withheld observations, not zero error. Approximately 1 Hz status samples
do not demonstrate continuous service or precisely measure outage durations.
The reported 328.413 ns causal USB forecast uses a different population and
requires the excluded raw capture for reconstruction; this verifier does not
recompute it. These statistics do not qualify the newly migrated firmware/client.

## Provenance and exclusions

The [import receipt](../../../papers/imports/research-2026-10-16.json) records
source/public hashes and the three USB JSON field redactions (private helper
path, source-scope identity and boot identity). Numerical results, denominators,
failed runs and limitations remain. The original derivation/build receipts keep
historical path names and original hashes; use the import receipt to locate their
public counterparts. A historical status such as author approval is a retained
source record, not approval to publish this migration.

Original raw USB/serial/RF captures, private source snapshots, enrollment and
installed images are excluded. Historical internal paths are provenance locators,
not files promised by this checkout. The USB raw-extraction script is not supplied
as a public reproduction command. The exported CSVs support the subset above;
they cannot independently reconstruct all physical claims. Older V14/PLL
collections retain their own manifests and are not rewritten by this import.

All three original source/PDF pairs match their recorded upstream build hashes.
All 36 pages were rendered and inspected during local import. The PDFs were not
re-typeset or silently revised. This is document/identity review, not independent
scientific peer review or promotion from draft status.
