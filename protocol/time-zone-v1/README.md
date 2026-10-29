# Coarse time zone extension v1

Candidate contract, 29 October 2026. Not hardware certified.
Copyright (c) 2026 Jonathan Yantis. MIT.

UTC remains primary. Add `uint16_le tz_id` and `int16_le tz_offset_min` as four
authenticated coarse metadata bytes. Manual CDT is `02 00 d4 fe` (ID 2,
America/Chicago, -300 minutes). Unknown is `00 00 00 80`; valid offsets span
-840 to +840 minutes. ID 1 requires offset zero. Unknown nonzero IDs with valid
offsets forward unchanged. Invalid metadata normalizes to unknown without
invalidating otherwise valid time. Malformed/authentication failures reject
the whole packet.

Offsets are zero-based. GM offsets below count from its `G7V4` body; GM AD
framing adds four bytes. RC capability offsets include its existing AD framing.
All existing pre-extension fields keep their previous offsets.

| Coarse message | Revision / discriminator | New bytes | TZ offset | 16-byte tag offset | Authentication domain |
| --- | --- | ---: | ---: | ---: | --- |
| GM ANN/STATUS/WITHDRAW | `G7V4`, revision 4 | 182 body / 186 AD | 162 body / 166 AD | 166 body | `PTOBTZ04` |
| Direct-GM JOIN/bootstrap/renewal | revision 5 | 168 | 148 | 152 | `PTOBTZJ5` |
| RC relay bootstrap/renewal control | revision 8 | 216 | 196 | 200 | `PTOBTZR8` |
| RC discovery capability | revision 8 | 148 | 128 | 132 | `PTOBTZC8` |

The tag authenticates the complete body before the tag under the existing
`r2_group_mac` algorithm and group key. GM ROLE/GNSS retain their old body
lengths but use `G7V4`, revision 4 and the new GM authentication domain.
Historical codec entry points remain available for offline historical fixtures;
normal TZ runtime entry points accept only these new coarse revisions.

Precision G1/GR3/RC7 payloads, their authentication, source permits and timestamp
coordinates remain unchanged. Unchanged precision bytes cannot bootstrap an
old coarse source through new-format admission.

From T096 source 0.15.206, the serving GM's current atomic pair can change with
its selected zone/calendar or explicit manual configuration. An RC learns it
from its authenticated selected parent and relays it unchanged. Its coarse lease is three advertising
periods: 750 ms for the 250 ms GM cadence, 300 ms for the 100 ms RC cadence.
Existing authenticated parent advertisements renew that lease independently of
precision correction availability, clock lock and map expiry. Each hop renews
its own presence lease. Do not preserve a precision-source deadline for TZ or
add TZ to precision packets. Expired live metadata reports unknown/UTC while
retaining the last accepted pair for matching presence renewal. An explicit
unknown update clears the live value; later presence cannot resurrect an older
known pair. Parent/path replacement clears metadata from the old context.

This supersedes .205's immutable first-pair cache. Direct RCs authenticate GM
ANN and check the admitted root/boot/epoch, sequence and original receipt age;
the existing coarse UTC anchor supplies a replay envelope, not precision or
timing permission. Initial JOIN metadata cannot downgrade a later ordered ANN.
Relay capability uses the existing `next_challenge`: the producer increments
its already monotonic admission challenge when the advertised pair changes.
Within the admitted path, a higher challenge accepts a changed pair; an equal
challenge renews only an identical pair. Older or equal-token conflicting pairs
are rejected. Fresh nonce-bound relay responses can bootstrap or renew that
same metadata scope. No counter or field is added to the wire.

The byte layouts, domains and precision payloads remain unchanged. All T096
participants need .209 or newer before relying on automatic DST. It retains
the existing passive scan after synchronization, so later coarse changes reach
the owner. An old .205 receiver deliberately retains its first pair; development
.206–.208 receivers miss later changes while their normal scan is stopped.

See the [design](../../docs/time-zones.md), [automatic TZ guide](../../docs/t096/auto-time-zone-plan.md)
and historical [T096 manual-wire test plan](../../docs/t096/time-zone-test-plan.md).

`vectors.json` contains four public synthetic CDT packets. The T096 host runner
checks their exact bytes and independently verifies their HMACs with Python’s
standard library. They are codec fixtures, not RF observations.
