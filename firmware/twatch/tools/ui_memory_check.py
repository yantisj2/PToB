#!/usr/bin/env python3
"""Check retained native compiler stack receipts for the Battery rendering path.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
This is a static direct-call budget, not measured hardware stack high-water.
"""
import argparse
import hashlib
import json
from pathlib import Path

def stack_frames(path):
    frames = {}
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) == 3:
            frames[fields[0]] = int(fields[1])
    return frames

def one_file(build, name):
    matches = list((build / 'target').rglob(name))
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one retained {name} receipt')
    return matches[0]

def size(frames, signature, required=True):
    matches = [value for name, value in frames.items() if signature in name]
    if len(matches) != 1 and (required or matches):
        raise ValueError(f'Missing or ambiguous native stack frame: {signature}')
    return matches[0] if matches else 0  # An inlined callee is in its caller's frame.

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    display = one_file(args.build, 'display.cpp.su')
    render = one_file(args.build, 'watch.cpp.su')
    d, r = stack_frames(display), stack_frames(render)
    frames = {
        'worker': size(d, 'void {anonymous}::worker(void*)'),
        'render': size(r, 'void watch::render('),
        'battery_app': size(r, 'void watch::{anonymous}::battery_app(', False),
        'history_totals': size(d, 'void {anonymous}::history_totals('),
        'window': size(r, 'watch::BatteryHistory::window('),
    }
    direct = frames['worker'] + max(
        frames['render'] + frames['battery_app'] + frames['window'],
        frames['history_totals'] + frames['window'],
    )
    stack, reserve = 6144, 1536
    passed = direct + reserve <= stack
    result = {
        'status': 'PASS' if passed else 'FAIL',
        'scope': 'STATIC_DIRECT_CALL_BUDGET_NOT_HARDWARE_HIGH_WATER',
        'task_stack_bytes': stack,
        'direct_call_bytes': direct,
        'helper_and_library_reserve_bytes': reserve,
        'unassigned_headroom_bytes': stack - direct - reserve,
        'frames': frames,
        'sources': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [display, render, Path(__file__)]},
    }
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'sources'}))
    if not passed:
        raise SystemExit(1)

if __name__ == '__main__':
    main()
