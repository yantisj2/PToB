# Protocol contracts

The [time zone wire plan](../docs/time-zones.md) adds four authenticated
bytes alongside coarse UTC: a stable zone ID and current offset, forwarded by
RCs. It requires an explicit new coarse-packet revision and leaves existing
precision layouts unchanged. The [candidate coarse-wire contract](time-zone-v1/README.md) records the
new lengths and authentication domains; hardware certification is pending. The [regional database proposal](../docs/time-zone-database.md) is a later GM
feature using the same fields.

The [T096 execution plan](../docs/t096/time-zone-test-plan.md) specifies
new-wire-only GM/RC tests with manual CDT before any other target rollout.
T096 source 0.15.205 is installed locally on TC-01 through TC-04; its
[bounded results](../docs/t096/time-zone-test-plan.md#local-205-results-29-october-2026)
retain incomplete GM successor gates and failed relay availability.

The [current T096 GM7 implementation](../docs/t096/gm7-implementation.md) uses
GM7v4+S2+C1+TZ1. The separate [GM7 draft 1](gm7/README.md),
[v2 baseline](gm7_v2/README.md) and later S1/S2/C1 contracts retain their own
frozen versions and synthetic fixtures; they do not establish hardware acceptance.

[RC8 LoRa version 2](rc8/README.md) freezes the offline T096 source/packet,
event-association, functional availability, freshness/authority and bounded-hold contract. Its portable
codec/policies have host tests; no radio owner or deployed RC8 service is enabled.
RC8 source class numbering is independent of RC7 BLE wire/release numbering.

The [V14-RC1 wire annex](v14_rc1/README.md) is frozen with six layouts and
synthetic known-answer vectors. Read the [V14 proposal](../papers/ptob-v14-sig-proposal.pdf)
and [specification](../docs/V14_CLOCK_SPEC_RC1.md) together with its documented
normative gaps. This candidate does not establish current firmware conformance.

The [V14 GD2 GM diagnostics extension](gm_diagnostics_v2/README.md) describes the
current T096 0.15.168/CoreS3-SE occasional GNSS and raw election-quality report,
including real GGA HDOP, strict legacy GD1 decoding and bounded 23 October 2026
field observations. Its descriptive cache grants no timing authority and does
not revise the frozen V14-RC1 timing layouts or qualify UTC accuracy.

Read the [v12-to-R2 background](../docs/V12_TO_R2.md) for the earlier service
direction. The [October 2 proposal](../papers/ptob-proposal.pdf) preserves an earlier
protocol edition; it should not be read as the current R2 wire contract.

The earlier R2 canonical contract import remains pending; it is distinct from
the V14-RC1 candidate. Versioned contracts, layouts, vectors, and their supporting
evidence must retain their own compatibility boundaries.

Every contract identifies its status and supported firmware profile. Proposed,
implemented, installed, and verified behavior are distinct. Illustrative traces
are labeled as synthetic and are not measurement evidence.
