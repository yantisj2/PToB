# Adopt a new target

Copyright (c) 2026 Jonathan Yantis. MIT.

Start with a receiver and a measurable local clock. Add publishing, relay or GM
service only when the hardware and evidence support that role. The implementation
examples are [T096](../firmware/t096/README.md), [W12](../firmware/w12/README.md)
and [SS3](../firmware/ss3/README.md); the [macOS client](../clients/macos/README.md)
shows a host consuming a USB source. PToB is experimental, not a certified profile.

## Radio and clock requirements

**Mesh participation requires Bluetooth LE periodic advertising, introduced in
Bluetooth Core 5.0.** A BLE version number alone is insufficient: periodic
advertising is optional. Check the actual chip, controller firmware, host stack
and exposed API together. Periodic advertising requires extended advertising and
Channel Selection Algorithm #2. See the Bluetooth SIG's
[Core link-layer specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/low-energy-controller/link-layer-specification.html)
and [LE primer](https://www.bluetooth.com/bluetooth-le-primer/).

| Role | Needed capabilities |
| --- | --- |
| Receiving RC/client in the mesh | Discover extended advertisements, establish/maintain periodic sync, receive the chosen wire payloads and associate each accepted event with a local timestamp |
| Relay RC | Receiver capabilities plus periodic advertiser, scheduling/concurrency for both directions, causal downstream timestamps and bounded freshness propagation |
| GM | Periodic advertiser, qualified independent reference-to-local-clock mapping, source authority/validity handling and measured or explicitly bounded transmit timing |
| Host/general client | Supported bridge such as USB or NTP; BLE periodic advertising is unnecessary if the host does not join the mesh |

Establish a proof of capability before porting the full application: create a
train, discover it, synchronize, receive continuously, lose it and reacquire it.
Check interval and payload limits, supported PHY, advertiser/sync counts,
concurrent radio use, callback lifetimes and controller version. Legacy
advertisement scanning, a GATT connection or a scan callback timestamp alone
is not the required periodic timing interface. PAST is a later optional sync
transfer facility, not a substitute for periodic advertising reception.

Provide a monotonic local counter with documented frequency, rollover, capture
latency, resolution and sleep behavior. Distinguish a timestamp captured at the
physical radio event from an interrupt or host callback delivered later. If the
controller cannot expose the event time, document that limitation and carry an
unknown or conservative timing bound; do not label a programmed TX estimate as
an actual capture. W12/SS3 experimental relay examples retain that distinction.

## Oscillator tuning and GNSS are optional

An ordinary free-running oscillator can support a software clock map: estimate
rate and phase against accepted observations, convert local ticks to source time,
and expose uncertainty/expiry. Physical frequency steering (DAC, trim register,
VCXO or other tuning) is optional. If added, characterize tuning range, step size,
settling, temperature response and interaction with the software estimator. An
actuator does not by itself improve measured accuracy.

Receiving RCs and relays do not require GNSS. A GM needs a valid reference and a
supported way to associate that reference's timescale with physical local events;
GNSS is one possible implementation. Another reference requires a new source
adapter and validation. PPS alone does not identify the second or UTC offset.
Document epoch, leap handling, reference uncertainty, loss detection and holdover.
An RC must not become a supposedly accurate GM just because its upstream is lost.
The selected T096 source adapter is an example, not proof that arbitrary reference
hardware works without changes.

## Protocol and security adapter

Select an exact implemented wire profile and source revision. The
[frozen V14-RC1 contract](../protocol/v14_rc1/README.md) is historical research;
current RC7 firmware has its own rules and tests. A paper title or version number
is not an interoperability test. Preserve encoded sizes, byte order, counter and
nonce semantics, authentication domains and expiry rules when adapting code.

Implement these boundaries explicitly:

1. **Identity and enrollment.** Define a stable nonzero 64-bit device ID, collision
   handling and exact byte order. Keep boot nonce, BLE address, display alias and
   optional UUID separate. Use the [shared registry](../registry/README.md),
   [identity rules](security/identity-and-names.md) and [PSK guidance](security/psk.md).
   Group authentication is not encryption or independent proof of device identity.
2. **Event association.** Match physical receive observations to their corrections;
   reject stale, duplicate, mismatched and replayed events. Preserve the original
   source's expiry through relay hops. Never renew authority solely by republishing.
3. **Clock map and validity.** Handle counter wrap and boot changes, refuse invalid
   maps, and expose missing/unknown quality. Maintain units and timescale conversions
   explicitly; UTC and relative phase are different claims.
4. **Role/controller ownership.** Give one owner authority over radio scheduling and
   state transitions. Handle parent loss, loops, resynchronization, source admission,
   watchdog recovery and bounded drain before updates. Port public production
   contracts and tests instead of recreating behavior from a historical simulator.
5. **Storage and updates.** Document bootloader, partition and reserved regions,
   enrollment storage, rollback and application-only update boundaries. Current
   examples use build-time keys; the [P1 proposal](t096/provisioning-design.md) is
   not an available generic provisioning library.

## Repository layout and review package

Add `firmware/<target>/README.md` (or `clients/<target>/README.md` for a host)
and link it from the root README and setup guide. Include supported board/pin
variants, available GM/RC roles, source version, license notices, public dependency
pins, build commands, identity/enrollment schemas and honest operating limits.
Keep platform adapters within the target. Reuse `registry/` rather than forking
another name map; a new target kind needs a coordinated schema/test update.

Import only explicitly selected source files with per-file origin and hashes in
`imports/`. Do not import private Git history, lab credentials or raw logs. Supply
a no-hardware builder, source/configuration/image receipt, host checks and an
inspector. Generated secrets and binaries belong in ignored owner-only output.
Use the project licensing policy and preserve third-party notices. Ordinary
verification must work without a sibling private checkout.

## Acceptance ladder

| Check | Evidence to retain | What it does not establish |
| --- | --- | --- |
| Public dependency/build reproduction | Exact source, toolchain, config and image identities; fresh-clone instructions | Hardware behavior |
| Offline protocol and state tests | Valid/invalid vectors, authentication, expiry, wrap, reboot, source loss and parent transition cases | Physical timing precision |
| Bounded hardware bring-up | Exact board/image, protected-storage preservation, readback, startup, status and recovery observations | Long-duration reliability or UTC accuracy |
| Interoperability | GM/RC combinations, direct and relay paths, failures/rejections and missing samples | Accuracy from lock alone |
| Independent measurement | All opportunities and validity populations, calibrated reference/instrument bounds, topology and run duration | Untested operating conditions |

Add fault and recovery cases before declaring support. Report denominators,
withheld samples and negative results. Treat receive-only operation, estimated
relay, measured relay and independently sourced GM as separate acceptance claims.
A new port may be useful before all roles are qualified if its README states the
boundary clearly.
