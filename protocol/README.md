# Protocol contracts

Start with the **30 October 2026 author-review [V14 SIG proposal](../papers/ptob-v14-sig-proposal.pdf)**
for the current service/profile design: GM7 succession, RC7 dynamic paths and
TZ/DST metadata. Its [edition manifest](../papers/editions/v14-sig-2026-10-30.json)
binds the reviewed source/PDF; it is not an adopted standard or hardware qualification.

The [T096 GM7 implementation guide](../docs/t096/gm7-implementation.md) describes
source **0.15.209**, profile **GM7v4+S2+C1+TZ1**. Root timing uses GR3; relay timing
uses RC7 version 7/profile 5. Public [GM codecs](../firmware/t096/protocol/gm7_codec.h)
and [RC7 codecs](../firmware/t096/protocol/rc_dynamic.h) provide their detailed bindings.

The [coarse TZ contract](time-zone-v1/README.md) specifies GM revision 4,
JOIN revision 5 and RC control/capability revision 8, including exact lengths,
authentication domains and four-byte zone/offset metadata. Precision layouts
and UTC coordinates remain unchanged. The [TZ overview](../docs/time-zones.md),
[T096 automatic selector/results](../docs/t096/auto-time-zone-plan.md) and
[W12/N30 ports](../docs/esp32-time-zones.md) define implementation and coverage.
The .209 direct follow-up passed; its separate relay window failed availability.
GM failover, a physical DST boundary and full certification remain open.

The following references retain their own version/evidence boundaries:

- [V14-RC1 annex](v14_rc1/README.md) and [specification](../docs/V14_CLOCK_SPEC_RC1.md):
  frozen 12 October fixed-route/connected-control candidate, six layouts and 33
  synthetic vectors. Its [normative issues](../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
  and historical research do not qualify current GM7/RC7.
- [GM7 draft 1](gm7/README.md), [v2](gm7_v2/README.md),
  [S1](gm7_simplified_s1/README.md), [S2](gm7_simplified_s2/README.md) and
  [C1](gm7_birth_c1/README.md): separately frozen design/source checkpoints;
  use the current implementation guide for their applicable rules.
- [GD2 diagnostics](gm_diagnostics_v2/README.md): the 23 October
  T096 0.15.168/CoreS3-SE descriptive report and bounded observations.
  It grants no timing authority; current GM7 ROLE/GNSS use their selected codec.
- [RC8 LoRa wire 3](rc8/README.md): separate configured functional fallback
  with unknown uncertainty/freshness and its own authority rules.
  [Wire 2](rc8/wire2-historical.md) remains historical, not a deployed-service claim.
- [v12-to-R2 background](../docs/V12_TO_R2.md) and
  [October 2 proposal](../papers/ptob-proposal.pdf): historical architecture and
  protocol editions, not current wire bindings.

Proposed, implemented, installed and qualified behavior are distinct. Preserve
frozen contracts/vectors; assign explicit versions/domains to incompatible
changes. Synthetic traces and checksums establish neither RF delivery nor accuracy.
