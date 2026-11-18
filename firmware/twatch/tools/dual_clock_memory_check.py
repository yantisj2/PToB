#!/usr/bin/env python3
"""Check retained native stack receipts for the new main-owner paths.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
from pathlib import Path
from ui_memory_check import one_file, stack_frames, size


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    files = {key: one_file(a.build, name + '.cpp.su') for key, name in
             [('main', 'native_main'), ('dual', 'dual_radio'), ('shadow', 'shadow_gnss'), ('lora', 'native_lora'), ('display', 'display')]}
    frames = {key: stack_frames(path) for key, path in files.items()}
    main_frame = size(frames['main'], ':void app_main()')
    poll = size(frames['dual'], 'void watchdual::poll(')
    edge = size(frames['dual'], 'void watchdual::edge(')
    witness = size(frames['dual'], 'void v12::G1PpsDiagnostic::observe(')
    # Reserve an additional 1 KiB on each new path for nested arithmetic,
    # capture and publication helpers, above the unchanged 4 KiB task reserve.
    paths = dict(presentation=main_frame + size(frames['main'], 'void {anonymous}::ui()') +
                              size(frames['display'], 'void n30_display_publish(') +
                              size(frames['display'], 'watch::Model watch::MobileSelection::select(') + 1024,
                 parallel_poll=main_frame + poll + witness + 1024,
                 parallel_edge=main_frame + size(frames['shadow'], 'void watchshadow::poll(') + edge + 1024,
                 pps_response=main_frame + size(frames['main'], 'void {anonymous}::input(') +
                              size(frames['main'], 'void {anonymous}::command(') +
                              size(frames['main'], 'void {anonymous}::lora_status_frame(') +
                              size(frames['dual'], 'void watchdual::frame(') +
                              size(frames['dual'], 'void watchdual::{anonymous}::radio_json(') + 1024)
    paths['snapshot_response'] = (main_frame + size(frames['main'], 'void {anonymous}::input(') +
                                  size(frames['main'], 'void {anonymous}::command(') +
                                  size(frames['main'], 'void {anonymous}::lora_status_frame(') +
                                  size(frames['dual'], 'void watchdual::snapshot_frame(') +
                                  max(size(frames['dual'], 'void watchdual::{anonymous}::radio_json('),
                                      size(frames['dual'], 'void watchdual::{anonymous}::lineage_json('),
                                      size(frames['dual'], 'void watchdual::{anonymous}::innovation_json(')) + 1024)
    radio = (size(frames['lora'], 'void {anonymous}::worker(void*)') +
             size(frames['lora'], 'void {anonymous}::worker_step(', False) +
             size(frames['lora'], 'void {anonymous}::publish(') + 1024)
    headroom = {key: 12288 - 4096 - value for key, value in paths.items()}
    headroom['lora_worker'] = 8192 - 2048 - radio
    result = dict(status='PASS' if min(headroom.values()) >= 0 else 'FAIL',
                  scope='SELECTED_STATIC_PATHS_WITH_RESERVES_NOT_HARDWARE_WATERMARK',
                  main_path_bytes=paths, lora_path_bytes=radio, headroom_bytes=headroom,
                  inputs={str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in files.values()},
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    a.out.mkdir(parents=True, exist_ok=False)
    (a.out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('status', 'headroom_bytes')}))
    if result['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
