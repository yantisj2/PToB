# T096 R2 firmware example

T096 R2 is the first firmware example being prepared for this repository. Its
direction comes from v12: periodic advertising for the precision timing stream,
with mesh participation separated from planned client services for less capable
devices. See [the architectural background](../../docs/V12_TO_R2.md).

**Source import is pending.** No buildable or deployable firmware is included yet.
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
