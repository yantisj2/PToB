# Group PSK

Status: current G1 semantics and proposed P1 provisioning. Public installation
and key management commands are not available yet. Copyright (c) 2026 Jonathan
Yantis. MIT.

G1 uses a random 32-byte group pre-shared key (PSK), a nonzero 64-bit group ID and
a nonzero 32-bit key epoch. Devices in one group need matching material. The
PSK authenticates protocol messages; it does not encrypt them. Anyone possessing
it can compute group MACs, including another member's claimed identity. Membership
alone neither grants GM authority nor establishes accurate UTC.

Create a group locally using OS randomness; share its enrollment material only
with intended members through a secure channel. Keep a private backup separate
from public source and support bundles. Never use an example/test key on a real
group or derive a key from a friendly name or device ID.

P1 proposes wired local provisioning of a generic image. Until implemented,
existing laboratory enrollment remains embedded in an image; neither that image
nor its generated headers are suitable public release assets. P1 storage holds
plaintext keys and does not claim resistance to a compromised host or physical
access. See [storage and local confirmation](../t096/provisioning-design.md).

Removing a name-map entry does not revoke a member. On loss or compromise,
replace the group key, advance the epoch, and reprovision every retained member;
exclude the removed member. P1 provides no seamless dual-key transition or
remote revocation. Coordinated reprovisioning can interrupt service. A lost PSK
cannot be recovered through the proposed device API; use the private backup or
create a new group. Group/epoch status is non-secret; key bytes are never returned.
