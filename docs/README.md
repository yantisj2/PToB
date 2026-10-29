# Design background

For the latest research, read [V14 research status](V14_RESEARCH_STATUS.md) and
the [frozen V14-RC1 clock specification](V14_CLOCK_SPEC_RC1.md). The status page
binds the paper, wire, and physical evidence checkpoints separately.

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

The [time zone wire plan](time-zones.md) records the proposed four-byte zone ID
and current UTC offset alongside coarse UTC, with manual GM configuration and
unchanged RC forwarding. The separate [compact regional database proposal](time-zone-database.md)
covers later GM offset calendars and rough GNSS selection for North America,
Central America and Western Europe. Source .206 implements that bounded GM
database/selector and dynamic receiver updates; hardware qualification is separate.

The [T096 GM auto-TZ guide](t096/auto-time-zone-plan.md) records modes, location
freshness, DST calendars, receiver update policy, verified data and limits.

The [T096 test plan](t096/time-zone-test-plan.md) stages a breaking manual-CDT
cohort on TC-01/TC-02/TC-04 before other target ports. Execution on the attached
cohort is authorized; certification is pending.

The [CoreS3-SE instrument](../firmware/corese/README.md) provides a dedicated
client and five passive diagnostic pages. Its [build guide](corese/build.md) and
[field provenance](corese/diagnostics.md) distinguish cached mesh roles, current
clock validity, GM uptime and locally observed source-epoch duration.
