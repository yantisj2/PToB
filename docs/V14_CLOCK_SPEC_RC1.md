# V14 clock protocol release candidate 1

Version: `V14-RC1`, frozen 12 October 2026. Status: **protocol specification candidate**. This specification defines portable clock behavior and wire profiles. Implementation conformance and measured results are recorded in a separate release report. Candidate status does not grant hardware, precision, or standards qualification.

V14 separates grandmaster operation from relay-clock operation. A grandmaster (GM) provides independently sourced time. A relay clock (RC) disciplines its local clock from an authenticated parent and originates a new downstream timing train. A client consumes either train. RC service operates without GM election membership and without waiting for a new parent correction before each transmission.

The requirements below use MUST, SHOULD, and MAY as requirement levels within this experimental protocol. The [machine-readable contract](../protocol/v14_rc1/contract.json), byte layouts, and synthetic known-answer vectors form the wire annex. Revisions to bytes, authentication domains, or clock semantics require a new explicit version; receivers MUST reject incompatible records rather than reinterpret them.

## Roles and authority

| Role | Clock input | Permitted behavior |
| --- | --- | --- |
| GM | An independently eligible source | Participate directly in authenticated GM coordination; originate source time when elected and admitted |
| Coordinator | Authenticated GM participants and their source evidence | Manage the bounded GM control group; coordination alone grants no timing precision |
| Standby | Its own eligible source, plus synchronization to the selected GM | Become HOT only with actual synchronization and current takeover authorization |
| RC | One selected authenticated timing parent | Discipline a local clock and originate downstream timing; preserve root provenance and inherited age |
| Client | One selected GM or RC timing parent | Read delivered time while its clock policy permits; originate no timing or GM authority |

Capabilities, configured profile, admission, elected role, actual serving, and clock quality are separate states. A device can support several profiles, but an RC profile MUST NOT become a GM, coordinator, or standby because it has a good fit, a held map, a source-looking display, or a renewed connection. Changing authority-bearing profiles MUST fence the previous scope before admission to the new one.

GM control is capped at four participants including the coordinator. Each remote GM participant MUST have a direct authenticated coordinator connection. Relay paths MUST NOT carry GM control or admit relay-only or overflow participants into that group. The cap does not limit ordinary clients or all authenticated identities.

Eligible candidates are ordered lexicographically by priority1, clock class, accuracy, variance, priority2, then stable identity, with smaller values preferred. Eligibility precedes ordering. Quality MUST describe actual evidence; unknown quality MUST NOT be promoted by numerical defaults, names, fit RMS, or uptime. The preferred candidate is GM. With a second eligible candidate, that candidate supplies the coordinator/standby assignment; HOT is a further synchronization state. Singleton operation requires an explicit local management path.

Startup MUST include at least 60 seconds of receive observation and 60 seconds of stable eligible source evidence before serving. These waits may overlap. A source epoch change, evidence expiry, receive fault, or invalid local time resets the affected admission. Source technology is a capability of an implementation; the protocol does not require a particular satellite receiver or MCU.

Each connected partition selects one eligible authenticated GM deterministically. Disconnected partitions may have distinct independently eligible GMs. Merge, accepted source change, reboot, and role withdrawal MUST invalidate the old live timing/authority scope before accepting its replacement. This is partition convergence, not global consensus. Retained old time, if a profile explicitly permits it, is HOLD without current GM entitlement.

The core takeover policy begins suspicion after two seconds without fresh authenticated root health and retains a three-second local source-change fence before replacement serving. Map loss alone is not root failure. These are takeover defaults, subject to source eligibility and scheduling, rather than a five-second guaranteed recovery deadline.

## Relay clocks and routes

An RC runs a local monotonic counter and a causal counter-to-time map. Accepted parent physical-event pairs discipline that map. Each outgoing timestamp MUST use the map effective at its own captured transmit event, with an immutable map generation. Later map changes MUST NOT improve or replace that historical timestamp.

An RC MUST NOT forward a parent packet as though the parent physically transmitted the RC event. V14 RC service is boundary-clock regeneration. Transparent residence correction is a different profile and is not implemented by this contract. The RC preserves root identity, root boot, source epoch, original issuance/expiry, UTC offset, and frame provenance while reporting its own sender boot, train, publication, capture epoch, map generation, and inherited age.

The fixed-route profile authenticates an acyclic route table. It supports up to eight table entries, root slot zero, three distinct RC slots, and at most two consecutive RC intermediaries. A transmitting child has a later slot and depth one greater than its parent; a leaf has no transmit slot and inherits its parent's relay depth. Slot availability does not grant role or source permission. The two-RC topology is a target capability with separate conformance evidence, not an automatic physical acceptance claim.

Routes and slots are configured policy. Peer addresses, boot identities, train incarnations, and source scope MUST still be discovered and freshly authenticated. No live dynamic slot allocator or unrestricted mesh is specified in RC1. A profile may constrain the allowed parent role or identity; parent selection MUST NOT silently introduce a direct-source or local-clock fallback into a relay-only path.

## Timing events and complete packets

The core timing frame is 62.5 ms (16 Hz). GM slot zero is followed by RC positions at 15.625, 31.250, and 46.875 ms. These are nominal positions for admitted controller scheduling, not guaranteed physical launch times. An implementation MUST reserve RX/TX resources through its supported controller, record actual placement, and advance by whole frames if preparation misses a position. A requested schedule is not a measured transmission.

Each timing advertisement is a complete 190-byte AD record containing an original publication identity and zero, one, or two corrections for earlier physical copies. An original MUST NOT contain a predicted timestamp for its own future transmission. A later correction names the earlier publication, periodic event, channel, and mapping generation. The receiver MUST match it to the saved physical RX capture of that original in the same boot/train/capture/source scope. Follow-up callback time and worker processing time are availability timestamps, not substitute RF timestamps.

The core wire profiles are:

| Record | Version | Size | Purpose |
| --- | --- | ---: | --- |
| GM precision | `GR`, version 3 | 190-byte AD | Root provenance, original source permission, and two physical corrections in a 16 MHz timestamp representation |
| RC precision | `RC`, version 5, profile 3 | 190-byte AD | Boundary-clock TX time in nanoseconds, per-correction map generation and cumulative age |
| RC local service | `RQ`, version 5, profile 3 | 136-byte record | Capability, request, and response for local authenticated demand/admission |
| GM/client join | type 14, version 4 | 164-byte record | Challenge-bound membership, coarse replay anchor, and local demand negotiation |
| GM management | type 17, Config5, C4 authentication domain | 168 bytes; HELLO 223 bytes | Direct GM coordination and role/source statements |

The 16 MHz representation is a **wire unit** (62.5 ns), not a required physical oscillator. Other clock hardware converts its actual event counter with checked rate/quantization handling. Neither this representation nor nanosecond fields imply nanosecond accuracy. Management records use the connected control bearer; the 223-byte HELLO is not a periodic advertisement.

All multibyte wire integers are little-endian. Signed fields use two's complement. The current manufacturer AD identifier is experimental `0xffff`; this is not an assigned production identifier. Unused correction entries and reserved fields MUST be zero. Timing records MUST reject zero required identity/incarnation fields, invalid routes, correction count above two, duplicated correction identities, nonpreceding correction publications, and out-of-range channels.

GM and RC precision records have distinct layouts and MAC domains. Translation MUST authenticate the incoming profile, preserve the root envelope's values, convert timestamp units explicitly, and create the appropriate outgoing profile statement. Re-signing an RC provenance statement does not create a new root observation or an individually verifiable GM signature.

Source issuance, expiry, frame and corrected TX time use the selected root clock coordinate. The authenticated signed UTC offset maps that coordinate to the profile's declared time scale; it MUST remain separate from relative oscillator training. The core uses a POSIX UTC estimate and makes no leap-second or absolute-UTC qualification claim. Signed hold-horizon/budget fields are precision-policy metadata: zero/zero means no bounded held-precision grant, and a nonzero pair requires separately supported bounds. They do not override the local functional hold policy or make UNKNOWN uncertainty known.

## Authentication and passive service

The RC1 trust profile uses a provisioned shared group PSK, group identity, and group epoch. Records use HMAC-SHA256 truncated to 128 bits with separate eight-byte domains and a versioned little-endian group context. Route authentication uses a truncated 64-bit digest within that group context. Fresh challenges, sender/receiver boots, monotonic sequences, and stream floors bind admission and fence replay. Display names are authenticated metadata, not authorization keys.

Every PSK holder belongs to the same symmetric trust boundary and can generate group-authenticated statements. The MAC does not prove unique hardware authorship or protect against a compromised group member. Public listeners may receive and parse broadcasts without a connection; receivers lacking the group key cannot verify this trust profile. Public signatures, delay-attack resistance, durable replay after arbitrary state loss, and production enrollment are separate work.

An authenticated local request MAY activate RC acquisition before a usable timing map exists. The response MUST distinguish admitted demand from timing readiness. A coarse bootstrap interval establishes replay/freshness context only and MUST NOT train the precision estimator or authorize source candidacy.

Steady timing reception is passive. The fixed-route service profile renews at approximately 300 seconds with a maximum 360-second local demand/admission lease and a bounded two-second request transaction. These are service-profile defaults, not accuracy or recovery guarantees. Upstream demand MUST reflect live downstream demand conservatively. Renewal MUST preserve still-valid same-scope maps, pending captures, and timing sync where controller resources allow; it MUST NOT rebase clock age or fabricate fresh source evidence.

Timing reception, discovery, admission context, downstream demand, and local output validity MUST be represented separately. Reconnection needs fresh proof even if logical membership was retained. No routine per-timestamp request/response exchange is required.

## Clock validity and holdover

Every delivered map records both the physical reference and the instant the usable correction became available. New maps take effect no earlier than that availability instant. A receiver MUST NOT apply later information to earlier output. Rejected originals, replay, authentication renewal, status reads, and retransmission MUST NOT reset the last accepted discipline instant.

The fixed-route experimental service profile permits local functional holdover for **strictly less than 500 ms after the last accepted discipline update**. At exactly 500 ms, output is unavailable. This is a provisional operating policy, not an oscillator error bound. A fresh accepted RC correction can renew a child's local map lifetime, but MUST carry increasing inherited age; it does not renew root evidence or imply a 500 ms end-to-end age bound.

Local update age, inherited map age, original root evidence age, source-serving permission, and authentication/demand expiry MUST remain distinct. GM GR3 source permission is at most 1.65 seconds. RC5's maximum 360-second admission/provenance representation MUST NOT be described as 360 seconds of fresh GM permission. RC held service can continue under its local policy without asserting the root is still live. Boot, source/counter epoch change, revocation, overflow, or a timebase fault MUST fence the affected maps; a holdover allowance cannot revive invalid authority.

The earlier windowed leaf profile has a separate 1.5-second reference policy clipped by its original permission. It is a compatibility profile, not the fixed-route 500 ms rule. A receiver MUST select one declared clock policy, rather than mixing the most permissive limits. An optional half-duty profile MUST keep its complete acquisition, recovery, and shutdown inside its declared listening cap, at most 50%; failures are recorded instead of extending the cap. RC1 fixed-route service does not claim a half-duty pass.

Functional states are UNAVAILABLE, TRACKING, and HELD; wire/reference compatibility may call TRACKING `LOCKED`. LOCKED or PLL activity is a software-clock state, not calibrated precision. Quality separately reports UNKNOWN/UNQUALIFIED or a supported conservative bound. Unknown capture, path, calibration, rate, and reference uncertainty MUST propagate as unknown, never zero. A bounded-precision claim requires the full applicable error budget and operating envelope, not RMS alone.

Within a continuous valid scope, delivered time MUST be monotonic. An unqualified discontinuity during reacquisition MUST be explicitly fenced/reported rather than presented as continuous precision. Expiry MUST invalidate served/displayed time and current fit/lock claims while discovery and reacquisition continue.

## Persistent service and resource safety

An installed normal-service profile MUST autostart and remain running after diagnostic capture finishes. A finite capture stops logging only. Healthy service has no diagnostic shutdown timer. Explicit stop, update drain, fault containment, and demand expiry retain their original bounded closure rules.

Capture and control processing MUST use bounded queues and histories with explicit overflow/refusal. Event association and map generations MUST not silently wrap or reuse live scopes. Exhaustion MUST fence/drain before allocation of a fresh authenticated scope. Capture callbacks MUST avoid unbounded work, formatting, allocation, or flash writes. Controller conflicts and an undrained transmitter are hard faults for the affected resource.

LoRa, WiFi, precision output, and other bearer extensions require their own profiles and evidence. The present BLE candidate grants none of those permissions. A constrained dual-bearer precision profile still requires its complete held delivered-time bound, including blackout and margins, to meet the separately established 500 ns gate.

## Conformance and release claims

Conformance MUST cover authenticated startup; original/follow-up association; malformed, replayed, and mismatched scopes; local holdover boundaries; demand renewal; source loss/recovery; role/profile fencing; partition merge; slot/resource conflicts; and persistent service after observers close. One-RC and two-RC paths, full listening and constrained listening, source diagnostics and independent witnesses are distinct populations.

Each implementation supplies separate source/artifact review, identity-bound installation, controller/resource evidence, and applicable physical checks. A specification freeze and a passing codec vector do not close those gates. Performance reporting MUST include signed bias, RMS including bias, jitter separately, tails, denominator, missing/unavailable intervals, reference uncertainty, and calibration provenance. One PPS sample per second does not establish intervening continuity.

This candidate supports publication of the boundary-clock architecture and correctly scoped prospective field results. Universal worst-case accuracy, continuous qualified precision, energy advantage, comparative superiority, calibrated absolute UTC, and Bluetooth/IEEE conformance require their own evidence.
