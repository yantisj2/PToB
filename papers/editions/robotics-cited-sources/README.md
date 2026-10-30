# Frozen robotics citation

The robotics paper’s [22 October manifest](../gptp-tsn-robotics-2026-10-22.json)
seals the earlier ESP32-S3 source cited during that review. The current S3
paper is a later edition. This directory preserves only the exact earlier
LaTeX bytes needed to reproduce that citation check without Git history.

[index.json](index.json) records its original path, byte count, SHA-256,
public repository revision and Git blob. The robotics source, PDF, sealed
verifier and original edition manifest are unchanged. No new paper edition
or evidence population is created.

Run `python3 tools/verify_robotics_edition.py` from the repository root. It
checks the current robotics edition and runs the original sealed verifier in
a temporary view containing this exact cited source. Missing or changed
snapshot bytes fail verification.

Copyright (c) 2026 Jonathan Yantis. This README uses MIT; the preserved paper
source uses CC BY 4.0 under [LICENSING.md](../../../LICENSING.md).
