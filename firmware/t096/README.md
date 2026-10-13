# T096 firmware example

T096 firmware, developed from the R2 application, is the first example being
prepared for this repository. Its direction comes from v12: periodic advertising
for the precision timing stream,
with mesh participation separated from planned client services for less capable
devices. See [the architectural background](../../docs/V12_TO_R2.md).

**Source import is pending.** No buildable or deployable firmware is included yet.
The [V14 papers](../../papers/README.md) and [RC1 contract](../../protocol/v14_rc1/README.md)
are now included. Their selected 0.14.43 research source is a historical identity,
not the migration baseline or a statement about current installed firmware.

The [R2 evidence collections](../../evidence/v12/r2/README.md) contain dated
experiment records; they are not a firmware distribution.

The example will include the application, board definition, required patches,
pinned dependencies, protocol tests, and build and recovery instructions.
A fresh checkout must build without a private sibling repository. Enrollment
is supplied or generated locally; private bench keys and enrolled images are
excluded from publication.

Hardware support and verified behaviors will be recorded against exact source
and image identities. Build or simulation success alone does not qualify hardware
precision. Firmware authority transfers only at an explicit documented milestone.
