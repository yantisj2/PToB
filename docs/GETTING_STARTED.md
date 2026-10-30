# Set up a PToB group

Copyright (c) 2026 Jonathan Yantis. MIT.

This guide describes a small experimental deployment: two T096 grandmasters,
optional RC receivers/relays, and a T096 USB source feeding a macOS client.
It is a source-build preview, not a public installation acceptance record.
Read each target's build and hardware boundaries before installing anything.

## 1. Choose the roles

```text
Independent source A -> T096 GM A --+
                                  +-> RC receivers -> optional RC relay -> receivers
Independent source B -> T096 GM B --+
                                  +-> T096 USB source -> macOS client -> chrony
```

Begin with both GMs and one receiver in direct radio range. Add relays only after
the direct path is understood. Two GMs supply source diversity only when their
references and failure modes are independent. The source-control plane selects a serving GM and an eligible synchronized
standby; two GM-capable devices are not two clocks to average. They do not establish automatic
failover quality, continuity or UTC accuracy by their mere presence.

| Component | Configuration | Important boundary |
| --- | --- | --- |
| GM A and GM B | Two T096 builds, each `--profile GM`, distinct actual device IDs | Each needs a supported, valid upstream reference; selecting GM does not create one |
| T096 receiver/relay | `--profile RC` | USB streaming is part of the default RC7 application, not a third profile |
| W12 receiver | `--profile RC --listening-percent 100` (or 50 for client-only use) | Optional `--estimated-relay` is experimental and has unknown TX uncertainty |
| SS3 receiver/relay | Selected RC target | Experimental estimated-TX relay; no GM implementation |
| N30 offline target | RC; names N3-1 and N3-2 | [Preparation/onboarding](n30/build.md); hardware bring-up pending |
| macOS client | USB source and client bridge | Host clock, chrony's user-space clock and independent UTC truth are distinct |

Use the [T096](../firmware/t096/README.md), [W12](../firmware/w12/README.md),
[SS3](../firmware/ss3/README.md) and [macOS](../clients/macos/README.md) guides
for the supported board and exact commands. The GM profile's current source
qualification and multi-GM limits are recorded in the
[GM7 operations](t096/gm-operations.md) and [implementation](t096/gm7-implementation.md)
guides. A receiver's lock indicator is not evidence
that a GM is independently accurate.

## 2. Create private enrollment and a shared map

Create a new group using a random 32-byte PSK, nonzero 64-bit group ID and nonzero
32-bit key epoch. The [PSK guide](security/psk.md) explains the security model and
key replacement. Use the same enrollment for group members, but a separate device
configuration for each board. Keep files and build products under ignored,
owner-only `artifacts/`; keep a private backup of enrollment separately.

Trusted credentials and runtime selection are separate. Discover available
meshes/sources locally rather than pinning a production image to one mesh ID;
require fresh authenticated admission after a context change.

Copy [the example registry](../registry/example.json) into your local inputs,
replace synthetic IDs with verified hardware IDs, and give each device a useful
alias. Supply the same `--registry` file to the target builders. Record both GMs,
all RCs and any known client identity in that one map. The map is display metadata;
adding a row grants neither membership nor source authority. A 64-bit wire ID is
not a 128-bit UUID; see [identity rules](security/identity-and-names.md).

Do not put a real PSK, generated enrollment header or enrolled binary in a commit,
issue or release asset. Current firmware requires rebuilding for enrollment/map
changes; the proposed mutable provisioning store is not available yet.

## 3. Build and validate each device separately

Acquire the pinned public dependencies using the target guide. Build GM A and
GM B into different fresh output directories with their own device configuration;
build the receiving T096 with RC as its default. Use the same group and registry.
Run the target's host checks and inspect its source/configuration/image receipt.
A deployment candidate requires a clean source commit. Diagnostic development
builds are explicitly not installation candidates.

The public repository does not yet ship a general flasher. Existing bounded
device updates are recorded in target cutover documents; they do not authorize
generic SDK flashing or mass erasure. Installation must bind the actual board,
preserve a known rollback image and protected storage, and verify application
readback. Follow the target's documented boundary rather than guessing a command.

Once installed through an accepted procedure, verify fresh firmware/build identity,
correct boot role and group/epoch, stable device ID and expected parent aliases.
Observe each GM's source validity before enabling downstream use. Record missing,
expired and rejected samples along with successful reception. Test source loss
and recovery separately before relying on the group in the field.

## 4. Add optional relay clocks

Place an RC where it can receive a valid upstream train and reach downstream
receivers. Check the actual selected parent and prevent loops; a display alias
does not establish topology. Add one hop at a time and retain source expiry and
uncertainty across the hop. W12 and SS3 estimated-TX modes do not provide measured
transmit timestamps or qualified relay precision. Additional hops require their
own availability, error and failure-recovery measurements.

## 5. Share time on macOS and with other clients

Follow the [macOS client guide](../clients/macos/README.md) to build the USB client,
check source status and feed chrony using its supported local bridge. A T096 RC
can supply the default RC7 USB stream; the reviewed normal GM backend has no USB
source-map publisher, and the SS3 CDC console cannot supply this interface.
Start with observation and the documented
private chrony configuration; preserve withheld/unavailable samples.

From the repository root, build and run the offline checks first:

```sh
sh clients/macos/chrony/build.sh
sh clients/macos/build.sh --registry artifacts/local/registry.json
clients/macos/ptob --self-test
```

After connecting a compatible, enrolled T096, `clients/macos/ptob` opens USB and
starts its owned private chronyd. `--no-chrony` runs the USB client without that
daemon. This is an operating step, unlike the offline self-test. The
[chrony guide](../clients/macos/chrony/README.md) covers local instance sharing and
read-only observation. The default configuration disables both NTP UDP and
command UDP ports; it shares a local user-space time service, not LAN NTP.

Chrony started with `-x` maintains a user-space clock without disciplining the OS
system clock. Applications reading the normal macOS wall clock will not thereby
read PToB time. They need the supplied client interface or an explicitly configured
time service. Changing the OS clock is a separate operating mode and is not part
of this quick start. See the [chronyd manual](https://chrony-project.org/doc/4.9/chronyd.html).

For network sharing, explicitly configure a listening address, port and permitted
client subnet; chrony's `allow` controls NTP client access. The migrated local
bridge is not automatically a LAN service. A private high port also requires
clients that can select that port. Do not add `local` merely to hide loss of an
upstream source: it changes how an unsynchronised server represents itself.
Review the [chrony FAQ](https://chrony-project.org/faq.html) before extending the
target's local Unix-socket setup. General NTP clients inherit host/network uncertainty,
not the precision of an internal BLE or USB timestamp.

Phones and desktops without usable periodic-advertising APIs are clients of a
bridge/time service, not automatically mesh participants. The proposed general
GATT client service is not yet supplied. For a native BLE port, continue with
the [target adoption guide](PORTING.md).
