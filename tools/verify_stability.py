#!/usr/bin/env python3
"""Verify the curated stability summary; no raw logs, hardware or daemons.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COLLECTION = ROOT / 'evidence/stability/2026-10-16'


def main():
    manifest = json.loads((COLLECTION / 'manifest.json').read_text())
    assert manifest['schema'] == 1
    for name, digest in manifest['public_files'].items():
        path = (COLLECTION / name).resolve()
        assert path.parent == COLLECTION and path.is_file(), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, name
    data = json.loads((COLLECTION / 'results.json').read_text())
    assert data['license'] == 'CC-BY-4.0'
    runs = {run['id']: run for run in data['usb_populations']}
    assert len(runs) == 4
    for run in runs.values():
        assert run['samples'] == run['precision_replies'] + run['missing_replies']
        assert run['precision_replies'] == run['valid_replies'] + run['invalid_replies']
        assert not run['independent_utc_qualified']
    assert runs['t096-67-source-loss']['invalid_replies'] == sum(
        runs['t096-67-source-loss']['invalid_reasons'].values())
    assert runs['t096-68-v4-controller-faults']['observed_mcu_restarts'] == 3
    assert data['retained_fault_review']['source_crc_valid_capsules'] == 4
    assert runs['t096-69-TC04']['fresh_helpers_after_first_failure'] == 10
    assert runs['t096-69-TC02']['request_errors'] == 0
    recovery = data['native_rx_recovery']
    assert recovery['attempts_completed'] == recovery['requests_prepared'] == 37
    assert recovery['owner_clears'] + recovery['baseline_trials'] == 20
    w12 = data['w12_observation']
    assert w12['status_frames'] == w12['following_frames'] + w12['held_frames']
    assert w12['stack_watermark_unit'] == 'bytes'
    audit = data['event_audit']
    assert audit['candidates'] == audit['bound_observations'] + audit['no_event_polls']
    assert audit['unique_event_keys'] <= audit['bound_observations']
    print(f"PASS {len(manifest['public_files'])} public hashes; four separate USB populations, "
          "native recovery, W12 and event accounting. Raw physical extraction not reproduced.")


if __name__ == '__main__':
    main()
