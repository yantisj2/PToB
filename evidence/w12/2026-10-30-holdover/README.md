# W12 sampled holdover duration — 30 October 2026

Selected inputs for Jonathan Yantis's approved
[MCXO-inspired holdover preprint](../../../papers/mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.pdf).
The canonical manuscript is [LaTeX](../../../papers/mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.tex).

| Population | Replayed result | Boundary |
| --- | --- | --- |
| C-1 warm rolling device grid | 64 s; 128 prefixes; P99/max 760/879 ns | Best measured window; later capture-end result 24 s |
| C-1 fixed mature long-run cohort | 75 s; 128 prefixes; P99 983 ns | Separate older cohort; one failure; 76 s fails with P99 1028 ns |
| C-2 first normal capture | ≥48 s; 128 prefixes; P99/max 606/880 ns | Later separate capture ends at 24 s |

Read [summary.json](summary.json) for compact cohort accounting and
[manifest.json](manifest.json) for every archive member's bytes, SHA-256,
record count and license. [observations.zip](observations.zip) contains the selected numeric
PAIR/START/END/STATUS/RESET records, UI snapshots, secondary output witnesses,
full provenance, predeclared protocol and reviewed source headers. Logical
`evidence/`, `protocol.json` and `reviewed-code/` paths inside the archive retain
their original names and exact bytes. Fixed metadata and sorted members make
the archive deterministic. It requires only Python's standard ZIP support.

Large derived prefix tables and full analysis are omitted from the loose tree;
regenerate them under ignored `artifacts/`:

```sh
python3 papers/mcxo-holdover-duration/tools/verify.py
python3 papers/mcxo-holdover-duration/tools/build_analysis.py --write
```

The verifier reproduces the original analysis and generated-table hashes,
including every retained failure and unknown. The original preparation seal
and former layout remain in Git at `87b314f`; this packaging changes no sampled
input or reviewed result. Public replay covers selected numerical observations,
not USB acquisition or private raw-console extraction. No raw console,
enrollment, device identity or firmware image is included.

These are frozen-clock software scores against live BLE witnesses. Overlapping
runs are correlated; gaps remain unmeasured. They do not qualify independent
UTC/electrical output, temperature compensation or physical GM failover.
Production temperature steering remains disabled.

Original selected records and derived summaries: **CC BY 4.0**, Jonathan Yantis.
Protocol/source manifests, source snapshots and documentation: **MIT**.
The original evidence provenance retains its dataset license, CC BY 4.0.
The archive manifest records the license of each member.
