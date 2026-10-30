#!/usr/bin/env python3
"""Run the sealed robotics verifier with its exact historical citation offline.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EDITION = "papers/editions/gptp-tsn-robotics-2026-10-22.json"
SNAPSHOTS = "papers/editions/robotics-cited-sources/index.json"
VERIFIER = "tools/verify_robotics_paper.py"


def relative_path(name):
    path = Path(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe edition path")
    return path


def matches(data, entry):
    return len(data) == entry["bytes"] and hashlib.sha256(data).hexdigest() == entry["sha256"]


def source_bytes(root, entry, snapshots, historical=False):
    data = (root / relative_path(entry["path"])).read_bytes()
    if matches(data, entry):
        return data
    # Only cited dependencies can resolve to a preserved historical snapshot.
    # The reviewed robotics paper, PDF and verifier must match their live paths.
    snapshot = snapshots.get(entry["path"]) if historical else None
    if snapshot is None:
        raise ValueError("Sealed bytes changed: " + entry["path"])
    if any(snapshot[key] != entry[key] for key in ("bytes", "sha256")):
        raise ValueError("Snapshot index disagrees with sealed manifest")
    relative = Path(snapshot["snapshot"])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Unsafe snapshot path")
    data = (root / relative).read_bytes()
    if not matches(data, entry):
        raise ValueError("Historical snapshot differs from sealed bytes: " + entry["path"])
    return data


def main():
    edition = json.loads((ROOT / EDITION).read_text())
    snapshots = {s["path"]: s for s in json.loads((ROOT / SNAPSHOTS).read_text())["snapshots"]}
    with tempfile.TemporaryDirectory(prefix="ptob-robotics-edition-") as folder:
        view = Path(folder)
        for entries, historical in ((edition["files"], False), (edition["cited_local_sources"], True)):
            for entry in entries:
                data = source_bytes(ROOT, entry, snapshots, historical)
                target = view / relative_path(entry["path"])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        manifest = view / EDITION
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_bytes((ROOT / EDITION).read_bytes())
        spec = importlib.util.spec_from_file_location("sealed_robotics_verifier", view / VERIFIER)
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        verifier.main()


if __name__ == "__main__":
    main()
