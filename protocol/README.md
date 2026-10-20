# Protocol contracts

[RC8 LoRa version 2](rc8/README.md) freezes the offline T096 source/packet,
event-association, functional availability, freshness/authority and bounded-hold contract. Its portable
codec/policies have host tests; no radio owner or deployed RC8 service is enabled.
RC8 source class numbering is independent of RC7 BLE wire/release numbering.

The [V14-RC1 wire annex](v14_rc1/README.md) is frozen with six layouts and
synthetic known-answer vectors. Read the [V14 proposal](../papers/ptob-v14-sig-proposal.pdf)
and [specification](../docs/V14_CLOCK_SPEC_RC1.md) together with its documented
normative gaps. This candidate does not establish current firmware conformance.

Read the [v12-to-R2 background](../docs/V12_TO_R2.md) for the earlier service
direction. The [October 2 proposal](../papers/ptob-proposal.pdf) preserves an earlier
protocol edition; it should not be read as the current R2 wire contract.

The earlier R2 canonical contract import remains pending; it is distinct from
the V14-RC1 candidate. Versioned contracts, layouts, vectors, and their supporting
evidence must retain their own compatibility boundaries.

Every contract identifies its status and supported firmware profile. Proposed,
implemented, installed, and verified behavior are distinct. Illustrative traces
are labeled as synthetic and are not measurement evidence.
