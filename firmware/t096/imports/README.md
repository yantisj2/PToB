# T096 source provenance

Copyright (c) 2026 Jonathan Yantis. MIT.

[2026-10-16.json](2026-10-16.json) records the explicitly selected private-source
inputs and the initial extraction hashes. Its `import_sha256` values are **not a
checksum manifest of the final public application**. Public build, profile,
identity and fixture adaptations followed extraction; 27 selected paths already
differ in the first public source commit `af3c9bb`. Subsequent edits are retained
in public Git history. The original extraction receipt is preserved unchanged.

Use a specific public Git commit to identify the application you review. The
[builder](../tools/build.py) records the complete current target source map,
dependency pins, generated local configuration, compiled source closure and
artifact hashes; the [inspector](../tools/inspect_build.py) checks that build.
Those private receipts and enrolled images are not release assets. Source and
binary identity do not establish hardware precision or installation acceptance.
