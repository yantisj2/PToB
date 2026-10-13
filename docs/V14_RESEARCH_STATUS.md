# V14 research status

The latest included drafts are the **12 October 2026 V14 proposal and research
preprint**, imported on October 13. They describe a passive clock service with
independently sourced GM authority, direct GM coordination, and dedicated relay
clocks regenerating downstream physical events through causal local maps.
Read [the papers](../papers/README.md) and [claim-linked data](../evidence/v14/2026-10-12/README.md).

| Material | Exact boundary | What it establishes |
| --- | --- | --- |
| October 2 manuscripts | Evidence cutoff 2026-10-02T17:28:13.400616Z | Historical calibration, causal publication, legacy transport and service results |
| October 3 oscillator study | Amended source; matching repository PDF exported October 9 | Numerical software-clock discipline; distinct from tunable hardware oscillators |
| V14 proposal/preprint | Edition October 12; evidence cutoff 2026-10-12T18:26:44.293859Z | Candidate profile and scoped prospective one-RC research |
| V14-RC1 wire | Specification checkpoint `7995249`; six layouts and 33 synthetic vectors | Frozen portable target; historical codec checks and known normative gaps |
| V14 physical research | Application 0.14.43; source `566ea2029c7ffc84a7d321df427c5c811fac2730` | One-RC PPS-shadow population and sampled renewal/expiry evidence |
| T096 firmware distribution here | Source import pending | No clean-build or deployment acceptance yet |

The one-RC experiment reports 62.504 ns RMS including bias on 436 usable causal
PPS-phase samples. Its complete denominator is 602 conservative opportunities:
436 usable, 65 invalid-reference, 3 expired-map, and 98 unobserved. These are
fractional-second software-map measurements after a separately trained provisional
correction, with 72.425% usable coverage. They do not establish calibrated UTC,
electrical output, or continuous precision between observed edges.

The [proposal issue record](../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
retains three open dispositions: GR3 origin-range notation, duplicate/nonpreceding
correction refusal, and incomplete meaning for a GC flag. Target requirements
and observed decoder behavior stay distinct. The research also leaves physical
two-RC validation, complete GM failover/merge, constrained-listening coverage,
W12 RC5 parity, reference/output calibration, uncertainty, and matched resource
measurements open. Later automatic-route or runtime-profile firmware requires
its own contracts and evidence; RC1 remains the frozen fixed-route edition.

The [import receipt](../papers/imports/v14-2026-10-13.json) binds source/PDF and
selected dataset hashes. [Fresh verification](../evidence/v14/2026-10-12/verification-2026-10-13.json)
checks document identity and included derivatives without operating hardware.
Draft status, source identity, firmware conformance, and measured qualification
remain separate.
