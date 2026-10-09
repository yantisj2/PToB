# From passive precision to v12 and T096 R2

[Project website](https://ptob.dev) · [Research papers](../papers/README.md) · [Evidence](../evidence/README.md)

This note records the project's architectural direction as of **9 October 2026**.
It explains the motivation behind v12 and the T096 R2 work. It is not a new wire
specification, an update to the dated papers, or a hardware qualification record.

## The requirement that changed the transport

The driving requirement for v12 was precision at a receiver consuming a timing
stream passively. A packet must be associated with the physical event it
represents, and a later correction must still refer to that original event.
Reception scheduling, correction availability, source changes, and periods
without a usable reference all affect what time the receiver can deliver.

That requirement led PToB to adopt **periodic advertising as a foundational mesh
capability**. The design uses a periodic timing stream to organize reception and
follow-up corrections. The v12 numerical model explored those choices under
explicit scheduling, loss, clock, and coordination assumptions; that work informed
the R2 implementation on the T096.

Periodic advertising support is necessary for the intended mesh role, but does
not by itself establish precision. Physical timestamp capture, event association,
clock mapping, controller behavior, and measurement uncertainty still need their
own implementation and evidence. Passive timing reception also does not mean
that a participating mesh node never transmits: coordination and authorized roles
can require separate control traffic.

## Mesh services and client services

The planned service split lets devices consume useful time without making every
device a mesh participant.

| Service path | Intended devices | Capability and status |
| --- | --- | --- |
| Mesh services | Embedded devices implementing a PToB mesh role | Periodic advertising support appropriate to the role is a baseline requirement. Timing, coordination, and authority remain distinct responsibilities. R2 is the current T096 implementation effort. |
| Client services | Phones, conventional operating systems, and devices that cannot meet the mesh requirements | A later GATT client service is planned. Its interface, timing quality, and supported platforms still need definition and validation. Consuming this service does not grant a mesh role. |

This is the PToB project's mesh architecture; it does not assert implementation
of the Bluetooth Mesh Profile. Likewise, a BLE version label alone does not
establish that a device exposes the controller and timestamp facilities required
for a particular PToB role.

**GATT has two different contexts here.** R2 experiments already use connected
GATT traffic for control and service coordination among experimental nodes.
That work is not the planned phone/OS client service. A future client API must
state its timestamp semantics, uncertainty, availability, and permissions rather
than inherit the mesh's precision claims by association.

## Read the evidence in sequence

| Stage | What it contributes | What it does not establish |
| --- | --- | --- |
| [October 2 papers](../papers/README.md) | Earlier passive-service proposal, calibration, and causal-publication research | Validation of later v12/R2 transports or wire formats |
| [v12 numerical model](../evidence/v12/model/README.md) | Executable hypotheses for schedules, corrections, coordination, and loss | Actual controller behavior or measured hardware precision |
| [R2 controller/PHY simulation](../evidence/v12/r2/simulation/README.md) | Implementation experiments with simulated sources and radio execution | Independent physical accuracy; the failed P1 campaign remains failed |
| [R2 hardware campaigns](../evidence/v12/r2/hardware/README.md) | Dated observations tied to particular T096 builds | Universal acceptance of later builds or every planned mesh/client feature |

The historical model, connected-control checkpoint, and P1 precision candidate
are separate revisions. Shared terminology does not imply wire compatibility.
Public passive access, half-duty clients, and relay precision are design goals;
the included R2 evidence does not establish completion of those services. The
planned phone/OS GATT service has no implementation or qualification claim here.

## Questions for technical review

- Which controller and host interfaces expose the event identity and physical
  timestamps needed for a defensible timing observation?
- How should mesh capability requirements and client-service capabilities be
  represented without overstating a device's precision or authority?
- What should a phone/OS client receive when a source is stale, a correction is
  missing, or timing uncertainty exceeds the application's needs?
- Which experiments would distinguish clock-map stability from delivered output
  accuracy, including startup, loss, recovery, and holdover?

Please connect feedback to a specific edition, profile, or experiment. The
[standalone T096 R2 example](../firmware/t096/README.md) remains a separate source
migration; no buildable firmware release is included in this repository yet.
