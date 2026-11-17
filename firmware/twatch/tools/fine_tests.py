#!/usr/bin/env python3
"""Offline actual RX17 owner, profile and frame guards; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
from fine_profile import profile, header, SCHEMATIC_SHA256, RELATIVE, ENDPOINT_CLASS

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cxx = os.environ.get('CXX', '/opt/homebrew/opt/llvm/bin/clang++'
                         if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
    fake_source = REPO / 'firmware/n30/tests/fine_fakes'
    device = dict(device_id='0000020000003001', board='lilygo_twatch_ultra_v1', radio='sx1262')
    reviewed = dict(schema=1, mode='TWATCH_ULTRA_RX17_RAW_SHADOW', factory=device['device_id'],
                    RX_GPIO=17, TX_GPIO=-1, board=device['board'], radio=device['radio'],
                    schematic_sha256=SCHEMATIC_SHA256, physical_review_sha256='1' * 64)
    production = dict(reviewed, mode=RELATIVE, phase80=125808,
                      endpoint_class=ENDPOINT_CLASS, phase_evidence_sha256='2' * 64)
    paths = [APP / 'main/fine_capture.cpp', APP / 'main/fine_capture.hpp', APP / 'main/fine_window.hpp', APP / 'main/window_diagnostics.cpp',
             APP / 'tests/fine_owner_test.cpp', APP / 'tests/fine_backend_test.cpp',
             APP / 'tools/fine_profile.py', Path(__file__)]
    spec = importlib.util.spec_from_file_location('watch_frame', APP / 'tools/framed_console.py')
    console = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(console)
    for name, selected in [('current', None), ('rx17', reviewed), ('g1', production)]:
        folder = out / name
        fakes = folder / 'fakes'
        fakes.mkdir(parents=True)
        # Selected public fake OS, adapted only for one fixed RX pin/IRQ.
        for src in fake_source.rglob('*'):
            if not src.is_file():
                continue
            dst = fakes / src.relative_to(fake_source)
            dst.parent.mkdir(parents=True, exist_ok=True)
            text = src.read_text()
            if src.name == 'fine_fake.hpp':
                text = text.replace('#define PTOB_N30_FINE_FACTORY UINT64_C(0x020000003001)\n', '')
                for old, new in [('#define PTOB_N30_FINE_RX_GPIO 4', '#define PTOB_N30_FINE_RX_GPIO 17'),
                                 ('#define PTOB_N30_FINE_TX_GPIO 5', '#define PTOB_N30_FINE_TX_GPIO -1'),
                                 ('pin==4||pin==5', 'pin==17'), ('gpio==4||gpio==5', 'gpio==17'),
                                 ('i<2&&PTOB_N30_FINE_PINS', 'i<1&&PTOB_N30_FINE_PINS'),
                                 ('(3u<<27)', '(1u<<27)'), ('const unsigned ch=pin==4?0:1;', 'const unsigned ch=0;')]:
                    text = text.replace(old, new)
            if src.name == 'fine_fake.hpp':
                text = text.replace('using gpio_num_t=int;', 'constexpr int GPIO_NUM_13=13;using gpio_num_t=int;')
                text = text.replace('c->flags.pos_edge==unsigned(i<1&&PTOB_N30_FINE_PINS)', 'c->flags.pos_edge==unsigned(i==1||(i<1&&PTOB_N30_FINE_PINS))')
                text = text.replace('x==(1u<<29)||x==(1u<<27)', 'x==(1u<<29)||x==(1u<<28)||x==(1u<<27)')
                text = text.replace('(x==(1u<<29)&&fake_clear_ok)', '((x==(1u<<29)||x==(1u<<28))&&fake_clear_ok)')
                text = text.replace('bits==(1u<<27)', 'bits==(1u<<27)||bits==(1u<<28)')
                text = text.replace('gpio==GPIO_MATRIX_CONST_ZERO_INPUT||', 'gpio==GPIO_MATRIX_CONST_ZERO_INPUT||gpio==13||')
                text = text.replace('assert(pin==17);++fake_gpio_calls', 'assert(pin==17||pin==13);++fake_gpio_calls')
            dst.write_text(text)
            paths.append(src)
        value = profile(device)
        if selected:
            file = folder / 'profile.json'
            file.write_text(json.dumps(selected))
            value = profile(device, file)
        (fakes / 'fine_capture_config.h').write_text(header(value) + '#include "fine_fake.hpp"\n')
        includes = [fakes, APP / 'main', REPO / 'firmware/n30/main', REPO / 'firmware/w12/main',
                    REPO / 'firmware/w12/legacy', REPO / 'firmware/w12/main/r2/r2-g1',
                    REPO / 'firmware/w12/main/r2/rc7/include']
        cmd = [cxx, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               *['-I' + str(x) for x in includes], str(APP / ('tests/fine_backend_test.cpp' if name == 'g1' else 'tests/fine_owner_test.cpp')),
               '-o', str(folder / 'owner')]
        if name == 'g1':
            join = REPO / 'firmware/w12/main/r2/r2-g1/group_join.c'
            obj = folder / 'join.o'
            subprocess.run([str(Path(cxx).with_name('clang')), '-std=c11', '-O1', '-g',
                            '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
                            '-c', str(join), '-o', str(obj)], check=True, timeout=60)
            cmd += [str(obj), '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections']
            paths += [join, REPO / 'firmware/w12/main/g1_corrected_clock.hpp',
                      APP / 'main/watch_holdover.hpp', APP / 'main/empirical_holdover.hpp']
        with (folder / 'compile.log').open('w') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        result = subprocess.run([str(folder / 'owner')], cwd=folder, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (folder / 'runtime.log').write_text(result.stdout)
        result.check_returncode()
        print(result.stdout, end='')
        rows = {}
        for frame_file in folder.glob('fine*frame.txt'):
            frame_rows = {}
            for line in frame_file.read_text().splitlines():
                frame_rows[line.split()[0]] = console.base.fields(line)
            console.validate_fine(frame_rows, selected if name == 'g1' else None)
            if frame_file.name in ('fine-frame.txt', 'fine-backend-frame.txt'):
                rows = frame_rows
        if name == 'rx17':
            for row, key, bad in [('N30_FINE_STATUS', 'TX', '1'), ('N30_FINE_STATUS', 'OE', '2'),
                                  ('N30_FINE_STATUS', 'channels', '7'), ('N30_FINE_STATUS', 'IRQ', '3'),
                                  ('N30_FINE_STATUS', 'pins', '18'),
                                  ('N30_FINE_CONTRACT', 'backend', '1'), ('N30_FINE_CONTRACT', 'phase80', '125440'),
                                  ('N30_FINE_CONTRACT', 'provenance', 'NATIVE_RW_SIGNAL'),
                                  ('N30_FINE_CONTRACT', 'RX', 'RW_RX_ON_238_GPIO18'),
                                  ('N30_FINE_EDGE0', 'kind', '1')]:
                changed = {k: dict(v) for k, v in rows.items()}
                changed[row][key] = bad
                try:
                    console.validate_fine(changed)
                except (AssertionError, ValueError, RuntimeError):
                    pass
                else:
                    raise AssertionError('Bad watch frame accepted: ' + key)
        if name == 'g1':
            for row, key, bad in [('N30_FINE_STATUS', 'TX', '1'), ('N30_FINE_STATUS', 'channels', '7'),
                                  ('N30_FINE_CONTRACT', 'phase80', '125440'), ('N30_FINE_CONTRACT', 'backend', '0'),
                                  ('N30_FINE_CONTRACT', 'provenance', 'NATIVE_RW_SIGNAL'),
                                  ('N30_FINE_CONTRACT', 'clock_update', 'NONE')]:
                changed = {k: dict(v) for k, v in rows.items()}
                changed[row][key] = bad
                try:
                    console.validate_fine(changed, production)
                except (AssertionError, ValueError, RuntimeError):
                    pass
                else:
                    raise AssertionError('Bad production watch frame accepted: ' + key)
            try:
                console.validate_fine(rows)
            except (AssertionError, ValueError, RuntimeError):
                pass
            else:
                raise AssertionError('Production watch frame accepted without manifest binding')
    for key, bad in [('RX_GPIO', 4), ('RX_GPIO', 18), ('TX_GPIO', 5), ('factory', '0000020000009999'),
                     ('board', 'meshnology_n30_heltec_v3'), ('mode', 'NATIVE_RW_GPIO4_5_FINE_CANDIDATE'),
                     ('physical_review_sha256', '0' * 64)]:
        changed = dict(reviewed)
        changed[key] = bad
        file = out / 'invalid-profile.json'
        file.write_text(json.dumps(changed))
        try:
            profile(device, file)
        except ValueError:
            pass
        else:
            raise AssertionError('Bad watch profile accepted: ' + key)
    for key, bad in [('phase80', 125809), ('phase80', True), ('phase80', 0),
                     ('phase_evidence_sha256', '0' * 64), ('endpoint_class', dict(ENDPOINT_CLASS, pdu_length=159))]:
        changed = dict(production)
        changed[key] = bad
        file = out / 'invalid-production-profile.json'
        file.write_text(json.dumps(changed))
        try:
            profile(device, file)
        except ValueError:
            pass
        else:
            raise AssertionError('Bad production watch profile accepted: ' + key)
    paths.append(APP / 'tools/framed_console.py')
    (out / 'receipt.json').write_text(json.dumps(dict(
        status='PASS', scope='ACTUAL_OWNER_FAKE_PERIPHERAL_ASAN_UBSAN_AND_FRAME_PROFILE_GUARDS',
        hardware_operated=False, production_backend='WATCH_RELATIVE_INPUT_FAKE_REGISTERS_ONLY',
        source_sha256={str(x.relative_to(REPO)): hashlib.sha256(x.read_bytes()).hexdigest() for x in set(paths)}), indent=2) + '\n')


if __name__ == '__main__':
    main()
