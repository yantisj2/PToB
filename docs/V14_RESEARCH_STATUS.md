# V14 research status

The **30 October 2026 author-review [V14 SIG proposal](../papers/ptob-v14-sig-proposal.pdf)**
describes GM7 broadcast ACTIVE/HOT/COLD succession, inherited mesh lineage,
RC7 dynamic paths and coarse TZ/DST metadata. Its
[edition manifest](../papers/editions/v14-sig-2026-10-30.json) binds source/PDF
and reviewed public source snapshots. It adds no physical experiment or accuracy
claim and remains unsubmitted, without standards adoption or publication approval.

The separate **12 October boundary-clock preprint** and
[claim-linked data](../evidence/v14/2026-10-12/README.md) retain their original
cutoff. Read [the paper catalogue](../papers/README.md) for exact editions.

| Material | Exact boundary | What it establishes |
| --- | --- | --- |
| October 2 manuscripts | Evidence cutoff 2026-10-02T17:28:13.400616Z | Historical calibration, causal publication, legacy transport and service results |
| October 3 oscillator study | Amended source; matching repository PDF exported October 9 | Numerical software-clock discipline; distinct from tunable hardware oscillators |
| Current V14 proposal | Author-review edition October 30; GM7v4+S2+C1+TZ1, GR3 and RC7/profile5 | Current protocol proposal; no new empirical qualification |
| V14 boundary-clock preprint | Edition October 12; evidence cutoff 2026-10-12T18:26:44.293859Z | Scoped prospective one-RC research |
| V14-RC1 wire | Specification checkpoint `7995249`; six layouts and 33 synthetic vectors | Frozen portable target; historical codec checks and known normative gaps |
| V14 physical research | Application 0.14.43; source `566ea2029c7ffc84a7d321df427c5c811fac2730` | One-RC PPS-shadow population and sampled renewal/expiry evidence |
| T096 reference source | 0.15.209; public source/build tools and scoped local results | Implemented GM7/TZ; build/install checks do not qualify failover or UTC/output accuracy |

Current T096 behavior is documented in the [GM7 guide](t096/gm7-implementation.md)
and [automatic-TZ results](t096/auto-time-zone-plan.md#four-device-follow-up--29-october-2026).
The four-device .209 follow-up passed its 600 s direct window; two receivers each
returned 600/600 fresh valid USB UTC records. Its separate 600 s relay window
failed availability: one leaf had the intended route/valid map in 96/116 samples,
with 20 unavailable conversions despite correct TZ in all 117 status replies.
Earlier failed windows remain recorded. Physical GM succession, a real DST
boundary, longer renewal/endurance and calibrated UTC/output remain unqualified.
The [W12/N30 ports](esp32-time-zones.md) have their own source and validation scope.

The one-RC experiment reports 62.504 ns RMS including bias on 436 usable causal
PPS-phase samples. Its complete denominator is 602 conservative opportunities:
436 usable, 65 invalid-reference, 3 expired-map, and 98 unobserved. These are
fractional-second software-map measurements after a separately trained provisional
correction, with 72.425% usable coverage. They do not establish calibrated UTC,
electrical output, or continuous precision between observed edges.

The **12 October RC1** [proposal issue record](../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
retains three open dispositions: GR3 origin-range notation, duplicate/nonpreceding
correction refusal, and incomplete meaning for a GC flag. Target requirements
and observed decoder behavior stay distinct. The research also leaves physical
two-RC validation, complete GM failover/merge, constrained-listening coverage,
W12 RC5 parity, reference/output calibration, uncertainty, and matched resource
measurements open at that cutoff. Current dynamic-route and runtime-profile
behavior has its own source/contracts and evidence; RC1 remains the frozen
fixed-route edition and does not qualify later firmware.

The [import receipt](../papers/imports/v14-2026-10-13.json) binds source/PDF and
selected dataset hashes. [Fresh verification](../evidence/v14/2026-10-12/verification-2026-10-13.json)
records that import's checks without operating hardware. Run
`python3 tools/verify_v14.py` for current proposal identity/coarse-wire checks
and the historical populations/tables. The replaced RC1 proposal pair resolves
through its exact hash-checked Git revision; include that history when cloning.
Draft status, source identity, firmware conformance, and measured qualification
remain separate.
