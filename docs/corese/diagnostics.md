# CoreS3-SE diagnostic field provenance

Copyright (c) 2026 Jonathan Yantis. MIT.

The display passively listens to mesh traffic. GD1 requires T0960.15.166 or later; GD2 adds real GGA HDOP with T0960.15.168
or later. Older producers leave the corresponding fields unavailable.
The client never manufactures a PTP dataset or treats display activity as a
renewed lease.

| Page | Source | Meaning and limits |
| --- | --- | --- |
| Mesh | Authenticated GS v1, 116-byte AD | Serving root/boot, source epoch, HOT/boot, coordinator, membership count, sequence and original receipt TTL. PRIMARY/HOT `UNCHANGED` timers track reported identity/boot events independently of packet cadence. |
| Path | Current timing-owner snapshot | Canonical admitted RC7 path, node boots, trains and epochs; direct source has one node. RC9 cannot stand in for an RC7 path. |
| Quality | Current fine client | Causal RMS, signed prediction residual, matched/refused counts, source receipt age and remaining model/permission TTL. Delivery percentage remains unknown when its event denominator is unavailable. |
| Reference | Authenticated GD2, 90-byte AD (legacy GD1, 88-byte AD) | Selected root/timing incarnation/mesh epoch; reported GNSS FIX/PPS/source-QUAL lamps, satellites, real GGA HDOP and the current election quality tuple. Reported metadata carries no timing authority or measured UTC accuracy. |
| Wire | GS decoder/monitor | Accepted, authentication/scope/age rejects and replays; root GM monotonic uptime extrapolated from authenticated `created_us`, locally observed epoch duration, sequence and report age. These are local observer counts; GS carries no epoch start timestamp. |

The source radio address must match the existing group-derived canonical address.
The selected root, boot and source epoch must match the timing owner. A replay
cannot refresh arrival time or validity. Accepted reports remain cached until a
newer authenticated selected-scope report replaces them during this observer boot.
A primary identity/boot change resets only the primary timer. A HOT identity/boot
change or explicit withdrawal resets only the HOT timer. Missing or expired GS,
source epoch updates, coordinator and membership changes do not reset those
identity timers. They keep advancing between reports; report age is shown
separately. They measure time since the last *observed* role event, not remote
oscillator lock or proof of uninterrupted remote operation. Cache retention never
extends the original GS validity window or any clock/source lease.
The bounded eight-context replay fence survives source selection changes within
the running image. It is volatile across device boots.

GS remains unchanged. The separate GD1/GD2 report uses the existing group-derived
canonical advertiser on SID12, a distinct HMAC domain and an independent
eight-context root/incarnation replay fence. Its boot field is the GM's
timing/profile incarnation, as in GS/G1, not its physical USB boot token.
The original local receipt plus authenticated valid interval (at most30s) determines
expiry. Duplicate sequence numbers, older remote sample times and replay after
scope changes cannot refresh receipt or expiry. The mesh epoch is a runtime
label: no deployment epoch value is compiled into the client. New GM identities
and timing incarnations use their own sequence namespaces.

Reference is diagnostic page4 and console `ui page 5`; navigation stays fixed.
The single AGE / CACHED indication shows local report receipt age. FIX/PPS/QUAL describe the
source's original sampled state, not a renewed local lock or source lease.
The UI omits RMC/PPS/GGA age rows. Their raw console values remain fixed ages
at the GM sample, never advancing remote measurements. Satellites zero is known
zero, distinct from unknown. A missing or different-scope report shows
dashes. A same-scope report remains visible after expiry as amber LAST REPORT /
CACHED data, with its actual report age. Known NO and zero remain distinct from
unknown. If selected timing scope temporarily becomes entirely empty, LAST GM / CACHED
retains that authenticated descriptive history. A different nonzero selection
hides it. Only the separate receipt AGE advances; the source flags
and PTP tuple are historical observations, never renewed current claims. Retained
report bytes never extend validity or reset receipt/expiry on duplicates.

`C` (clockClass), `ACC`/`VAR`, and `P1`/`P2` are the actual reported election
tuple. Unknown accuracy codeFE displays ACC UNKNOWN; absent varianceFFFF
displays VAR NONE. Other accuracy/variance codes retain their hexadecimal value. It does not establish IEEE1588 transport conformance, calibrated
UTC accuracy or traceability. HDOP is the dimensionless checksum-valid GGA value, displayed with two decimal
places only when the authenticated GD2 known flag is set. GD1 has no HDOP and
cannot populate it. Blank, unsupported, stale or malformed HDOP remains unknown.
HDOP is receiver geometry, not measured timing quality. PDOP, VDOP, antenna
health, timePropertiesDS and IEEE timeSource remain unavailable; GD1/GD2
source-kind1 means local GNSS only.
JOIN GNSS capability is not a substitute for GD1 sampled state. `mesh diag`
exposes separate `gd_` scope, original receipt/expiry, wire version, sequence, flags, fixed
source ages, HDOP centi-units, tuple and acceptance/rejection counters for bounded field verification.

`status`, `fine read 0` and `mesh diag` are separate CRC-protected snapshots.
Timing permission can expire or recover between their request timestamps. A
status reply with an empty selection followed by a live diagnostic reply does
not establish that both states existed simultaneously. When comparing scopes,
bracket the diagnostic with normal status replies and retain changed or empty
selections as unknown comparisons. Keep those samples and their denominators;
check each report's original receipt and expiry independently. Even matching
brackets are an observation interval, not an atomic timing-owner snapshot or
proof of uninterrupted source availability.

Diagnostics never query or modify a GM. Scanning is passive: it sends no
BLE scan requests. When a protected healthy fine clock permits, an80ms local
listening window may open at most every800ms. Normal timing admission/renewal
remains independent; reading a screen or its cache triggers no master request.
GD-only occasional windows do not clear GS role history or reset its independent
PRIMARY/HOT observation timers.

RC9's authenticated 190-byte time snapshot contains functional UTC plus some
upstream link metadata. That RMS is a remote link observation, never the CoreSE's
fine causal RMS or an independent UTC bound. Current CoreSE rendering withholds
local precision readouts whenever RC9 supplies the UTC display.

WIRE uses stable cached labels and values between reports. `GM UPTIME EST` is a
coarse extrapolation of the GM boot clock at its last report, never UTC. `EPOCH
SEEN` starts when this client first accepts the reported mesh epoch label and
persists across root/timing-incarnation replacements at that same label. A new
label restarts it; it is not the epoch's actual creation time. Durations use compact day/hour/minute
notation. Neither GS expiry nor the latest rejected packet clears these values.
