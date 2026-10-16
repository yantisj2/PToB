> October11 direct-client revision: canonical sources now match frozen T096
> 94ef87c06d4c47e37f1a1a77eb9ac61370e5200f. JOINv4 is164 bytes with
> PTOBG4J4, source_bound_us at144 and tag at148; authentication lifetime5s
> and time extraction250ms remain separate. GR3 uses PTOBG3O3/PTOBG3R3,
> signed1650ms maximum, depth-zero only; capabilities/control/demand remain0.
> Source hashes and independent V14 vectors are in r2_g1_client.json.
> The historical wire specification below is retained for legacy tests only.
> Physical event/channel/edge gates still prevent oscillator training on
> unqualified descriptor stamps. No measured precision or GM readiness claim.

# Group PSK profile G1 — implementation contract

Status: development, pending final controller validation and immutable release
binding. This document is not authorization to flash or resume W12 engineering.

## Identity, membership and control

Provision one 256-bit group PSK, nonzero 64-bit group ID and nonzero 32-bit key
epoch through private ignored material. A device supplies its stable nonzero
64-bit silicon identity and a display name when joining. Names are 1–24 UTF-8
bytes; duplicate names and renames are allowed. Names are never keys, identities,
election tie breakers or evidence of source quality. NUL, control, malformed,
overlong, surrogate and directional-control text is rejected.

The four-participant limit applies only to GM control, including the coordinator.
Three persistent control peer connections and one transient admission connection
fit the controller's four-connection limit. Every unknown peer authenticates on
the transient connection before control admission is negotiated. A GM-capable
device can join a full core as a passive client. A local eight-entry recent-name
cache is display metadata; eviction never revokes membership or changes identity.
There is no per-client recipient-MAC list in timing packets.

The T096 image retains its separate silicon-bound TC-01/02/03 hardware safety
and maintenance checks. Those deployment checks do not restrict the identities
of remote G1 peers. A W12 implementation uses its own verified factory identity.

Resource limits remain finite: one active join exchange, a five-second exchange
deadline, bounded GATT queues, and bounded reconnect attempts. Occupied admission
capacity leaves another requester waiting for discovery/retry; a one-entry,
five-second cooldown after successful transient admission prevents that peer
from immediately monopolizing repeated discovery windows; it does not create
an additional control participant. A join request alone grants no membership.
An unknown incoming connection is provisionally bounded and its claimed identity
is accepted only after verifying the request MAC and canonical peer address.

## Symmetric trust

G1 authenticates possession of the group PSK. Every PSK holder can calculate every
group MAC, including a root-origin MAC or another member's claimed identity. It
does not provide non-forgeability against malicious group members. Root and hop
domains preserve protocol provenance and immutable expiry for conforming peers;
they are not asymmetric root signatures. A separate root signing key would be a
different protocol and is not claimed here. Capability claims are authenticated
claims only. Existing source, direct coordinator, election, replay and precision
gates remain required before serving as GM. Membership alone grants no GM role.

## Cryptographic serialization

All integers are unsigned little endian except the documented signed UTC offset.
MAC is the first 16 bytes of HMAC-SHA256 with the 32-byte PSK over:

`domain[8] || version_u8(1) || group_id_u64 || key_epoch_u32 || body`

Domains are exactly `PTOBG1C1` (control), `PTOBG1P1` (direct timing),
`PTOBG1O1` (immutable root origin), `PTOBG1R1` (relay hop), `PTOBG1J1`
(join), and `PTOBG1K1` (identity-pair derivation). Pair derivation body is
`sender_id_u64 || receiver_id_u64`; its 16-byte result supplies legacy internal
adapters. The canonical random-static BLE address is the first six bytes of
K1(self,self), with the two high bits of byte 5 set. Packet MACs use the group PSK
directly with their distinct domains. Comparisons are constant time.

Control wire version byte is 13: HELLO is 223 bytes, other control records 168.
The existing control body (without its eight-byte legacy tag) is followed by
key epoch, four sender-local control-slot identities, HELLO name/capability
metadata when applicable, and the 16-byte G1 tag. Slot dictionaries are remapped
by stable identity at reception. An unknown optional pair hint is omitted rather
than preventing authentication of the sender. It grants no partner authority.

Direct timing remains 158 bytes, marked `G1`; root identity is at byte 126,
key epoch at 134, reserved zero bytes at 138, and P1 tag at 142. Relay timing is
190 bytes, marked `GR`: key epoch at 10, immutable 60-byte origin at 14,
O1 tag at 74, 46-byte route at 90, two correction slots at 136, R1 tag at 174.
The origin is root ID, designated relay ID, root boot, source epoch, creation,
expiry, frame and signed UTC offset (8,8,8,4,8,8,8,8 bytes). The hop tag covers
the entire preceding relay packet including the origin tag. Depth two is refused.

## Join exchange

Join wire size is 160 bytes. Header bytes 0–3 are 14, kind, version 1,
capabilities. REQUEST=1, CHALLENGE=2, PROOF=3, ACCEPT=4. Offsets and byte widths:

| Offset | Field | Bytes |
|---:|---|---:|
| 4 | key epoch | 4 |
| 8,16 | sender, receiver identities | 8 each |
| 24,32 | sender, receiver boots | 8 each |
| 40,48 | client, server nonces | 8 each |
| 56,64,72 | sequence, creation, causal echo | 8 each |
| 80,88 | source root identity, boot | 8 each |
| 96,100 | source epoch, timing publication/train | 4 each |
| 104 | coarse UTC microseconds | 8 |
| 112 | original remaining source permission, microseconds | 4 |
| 116,117 | source flag, name byte count | 1 each |
| 118 | zero-padded name | 24 |
| 142,143 | negotiated control bit, reserved zero | 1 each |
| 144 | J1 tag over bytes 0–143 | 16 |

The lower stable ID initiates the connection. Both boots and fresh nonces bind
the exchange; sequence numbers strictly increase. Every echo is checked against
the local emission time and must return within 250 ms. This is a replay bound,
not a clock-accuracy claim. The responder can downselect requested control
admission; neither endpoint may upgrade an unrequested control slot. Promotion
to a control slot waits for the join send to drain, then starts ordinary control
authentication. The same join records support either connection direction for a
passive client; PROOF or ACCEPT can carry the current source bootstrap.

The GATT service UUID is `850d0400-a6ef-4775-974b-188a7d4ed45c`; the write-without-
response characteristic is `850d0401-a6ef-4775-974b-188a7d4ed45c`. Discover the
handle through supported GATT APIs; do not assume a numeric handle. Required ATT
MTU is 226, ACL payload 230. GATT discovery completes independently at each end;
retain a verified incoming request within its original exchange deadline while
waiting for local discovery. Release connectable advertising's reserved host
connection object before initiating the fourth transient connection.

Source bootstrap subtracts the full causal exchange time plus the independent
coarse-map bound from the sender's original remaining permission. Its midpoint
UTC estimate carries half the round trip plus that independent bound as its
uncertainty; the100ms admission uncertainty limit remains separate from expiry.
Both calculations add `ceil(round_trip_us/500)+64us` for the declared2000ppm
clock drift and two32kHz tick quantizations.
Expired or
excessively delayed records cannot restart a source lifetime. Bootstrap grants no
precise map. Clients obtain samples only from authenticated physical RX/correction
association. A coarse anchor grows its carried uncertainty at the declared
2000ppm map-drift budget plus100us margin, and expires before that uncertainty
reaches the250ms coarse replay-age budget. This is a lab bound assumption, not
absolute UTC qualification. It must refresh through a fresh exchange; it does
not extend the independently checked signed source deadline. Ordinary clients
request the existing exchange at cold start,100ms growing anchor uncertainty
(125ms urgent), or after continuous1.5s source-permission loss or a fresh scope
change. These availability thresholds leave the249900us hard gate unchanged. They keep no
server membership record and no recurring demand lease. Passive timing
renewal retains the same root boot/source/train scope
and signed original expiry. A new source scope requires fresh authorization.

## Timing and migration

GMs and relays remain online. Public clients open a 400-ms window per second;
actual controller RX union and quiet intervals must satisfy the 50% budget.
Discovery scans continuously inside that window; it does not apply a second
50% throttle within the already bounded window. Expired source permission remains unusable immediately. A continuous1.5s loss
requests a fresh causal binding; a same-scope signed original arriving sooner
may reuse the still-valid coarse anchor, never the expired permission. The public
client initiates only while a bootstrap is needed, inside the first100ms of the
autonomous window; the explicit field treatment uses200ms inside450ms. Both
retain their original close and measured500ms contiguous quiet requirement. Group
connectable discovery uses31.25–41.25ms intervals; legacy roster discovery retains
100–110ms. An overflow client remains a client for this boot and cannot form a
second GM core with other overflow clients; reboot performs fresh autonomous
control admission. The
250-ms physical-reference freshness requirement is separate from the up-to-1.5-s
immutable root-origin lifetime, advertisement interval, estimator fit and source
authorization. Ten-second fit and seven-second quality dwell are unchanged.
Intentional quiet periods preserve estimator history but provide no new samples.
Root boot, source epoch, capture epoch or train changes fence incompatible state.

G1 is an explicitly versioned successor to per-recipient roster authorization.
Legacy packet decoders remain for the legacy provisioned profile; mixed profiles
do not silently authenticate each other. All intended peers require the same
private group provisioning and G1 implementation. Key rotation requires an
explicit coordinated provisioning update; no unauthenticated over-air key update
or per-device name-based secret is provided.

Absolute UTC, delivered output accuracy, formal Bluetooth qualification and W12
interoperability remain UNKNOWN until their separate evidence gates pass. Final
release receipt and W12 handoff must bind this contract, public vectors, source
commit, config, ELF and UF2 to completed tests before adoption.
