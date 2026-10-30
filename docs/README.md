# Design background

Start with the **30 October 2026 approved public-review
[PToB Viability preprint](../papers/ptob-viability-preprint.pdf)** for the
current research synthesis. Its [LaTeX source](../papers/ptob-viability-preprint.tex),
[source and evidence audit](../papers/editions/viability-2026-10-30-audit.json)
and [publication approval](../papers/editions/viability-2026-10-30-publication.json)
preserve the reviewed edition and its limits.

For the current protocol proposal, read the **30 October author-review
[V14 SIG draft](../papers/ptob-v14-sig-proposal.pdf)** and
[V14 research status](V14_RESEARCH_STATUS.md). The
[V14-RC1 specification](V14_CLOCK_SPEC_RC1.md) is a frozen 12 October reference;
paper editions, wire revisions and physical evidence retain separate identities.

Start with [From passive precision to v12 and T096 R2](V12_TO_R2.md): why periodic
advertising became a mesh requirement, how mesh and client services are being
separated, and where the planned phone/OS GATT service fits.

Use the [papers](../papers/README.md) for dated research and the
[evidence index](../evidence/README.md) for experiment-specific claims. Design
direction, implemented behavior, and measured outcomes have different scopes.

The [RC round-trip ranging proposal](t096/round-trip-ranging.md) studies a
GM-granted, low-priority T096 exchange, conditional averaging, shadow-PPS
validation and per-link propagation correction. It includes reproducible
synthetic arithmetic; ranging and automatic compensation are not implemented.

The [GM7 operations guide](t096/gm-operations.md) covers ACTIVE/HOT/COLD setup
and observation; the [implementation guide](t096/gm7-implementation.md) defines
current source behavior and compatibility.

The [time zone contract overview](time-zones.md) describes four authenticated
coarse bytes, dynamic updates and three-period leases. T096 .209 supports GM
location/calendar modes and updates after receiver timing lock; the
[auto-TZ guide](t096/auto-time-zone-plan.md) records coverage and finite results.
[W12/N30 ports](esp32-time-zones.md) consume the same metadata.
The [regional design](time-zone-database.md) and
[manual-wire test plan](t096/time-zone-test-plan.md) retain their dated scope;
physical DST, failover and full certification remain open.

The [CoreS3-SE instrument](../firmware/corese/README.md) provides a dedicated
client and five passive diagnostic pages. Its [build guide](corese/build.md) and
[field provenance](corese/diagnostics.md) distinguish cached mesh roles, current
clock validity, GM uptime and locally observed source-epoch duration.
