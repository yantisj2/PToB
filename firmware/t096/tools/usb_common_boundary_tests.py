#!/usr/bin/env python3
"""Exact generated USB lifecycle bodies at W1C-aware synthetic boundaries.

Uses public pinned SDK inputs. This checks software ordering and ownership,
not a physical USB trigger, silicon behavior or field recovery qualification.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

from usb_ep0_recovery import generate

APP = Path(__file__).resolve().parents[1]


def function(body, name):
    match = re.search(r'^.*?\b' + name + r'\([^;]*?\)\s*\{', body, re.M)
    assert match, name
    at = body.index('{', match.start())
    depth, end = 1, at + 1
    while depth:
        depth += (body[end] == '{') - (body[end] == '}')
        end += 1
    return body[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    generate(args.deps / 'zephyr', out / 'common.c', 'usb-dma-health')
    common = (out / 'common.c').read_text()
    manifest = json.loads((APP / 'cmake/usb-dma-health.json').read_text())
    legacy = (args.deps / 'zephyr' / manifest['path']).read_text()
    fixture = APP / 'tests/usb_common_boundary_fixture.cpp'
    names = ('r2_usb_common_failed', 'r2_usb_fail', 'r2_usb_dma_take',
             'r2_usb_wait', 'nrf_usbd_dma_finished', 'nrf_usbd_common_stop',
             'usbd_enable', 'nrf_usbd_common_enable',
             'nrf_usbd_common_disable', 'nrf_usbd_common_suspend',
             'nrf_usbd_common_ep_abort', 'nrf_usbd_common_ep_disable',
             'usbd_dmareq_process', 'nrf_usbd_common_ep_transfer',
             'ev_usbevent_handler', 'nrf_usbd_common_wakeup_req')
    fixed_functions = {name:function(common, name) for name in names}
    inputs = [fixture, Path(__file__), APP / 'cmake/usb-dma-health.patch',
              APP / 'cmake/usb-dma-health.json', APP / 'tools/usb_ep0_recovery.py']
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {str(path):sha(path) for path in inputs}
    variants = {'fixed': fixed_functions.copy()}
    old = fixed_functions.copy()
    for name in ('nrf_usbd_common_stop', 'nrf_usbd_common_disable',
                 'usbd_enable', 'nrf_usbd_common_enable'):
        old[name] = function(legacy, name)
    variants['legacy'] = old
    no_launch = fixed_functions.copy()
    no_launch['usbd_dmareq_process'] = no_launch['usbd_dmareq_process'].replace(
        '\tif (r2_usb_common_failed()) return;\n', '').replace(
        '\t\tif (r2_usb_common_failed()) { k_sem_give(&dma_available); return; }\n', '')
    assert no_launch['usbd_dmareq_process'] != fixed_functions['usbd_dmareq_process']
    variants['negative-launch'] = no_launch
    no_acquire = fixed_functions.copy()
    no_acquire['r2_usb_dma_take'] = no_acquire['r2_usb_dma_take'].replace(
        'if (!r2_usb_common_failed()) return true;', 'return true;')
    variants['negative-acquire'] = no_acquire
    no_wake = fixed_functions.copy()
    no_wake['ev_usbevent_handler'] = no_wake['ev_usbevent_handler'].replace(
        'm_bus_suspend && r2_wakeup_requested && !r2_usb_common_failed()', 'm_bus_suspend')
    variants['negative-wake'] = no_wake
    compiler = os.environ.get('CXX') or (
        '/opt/homebrew/opt/llvm/bin/clang++'
        if Path('/opt/homebrew/opt/llvm/bin/clang++').is_file()
        else shutil.which('clang++'))
    assert compiler, 'C++ compiler required for W1C register proxy fixture'
    results = []
    for name, bodies in variants.items():
        source = '\n\n'.join(bodies.values())
        # The host DMA boundary does not dereference the MCU address payload.
        source = source.replace('(uint32_t)&m_dma_odd', '(uintptr_t)&m_dma_odd')
        source = source.replace('(uint32_t)(NRF_USBD)', '(uintptr_t)(NRF_USBD)')
        include = out / (name + '.inc')
        include.write_text(source)
        binary = out / name
        cmd = [compiler, '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-sanitize-recover=all', '-DR2_COMMON_BODIES="' + str(include) + '"',
               str(fixture), '-o', str(binary)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        (out / (name + '-build.log')).write_text(build.stdout + build.stderr)
        build.check_returncode()
        cases = ['negative-suspend', 'negative-held', 'negative-ready'] if name == 'legacy' else ['']
        for case in cases:
            timeout = False
            try:
                run = subprocess.run([str(binary), *([case] if case else [])],
                                     capture_output=True, text=True, timeout=1)
                output, rc = run.stdout + run.stderr, run.returncode
            except subprocess.TimeoutExpired as exc:
                timeout, rc = True, None
                output = (exc.stdout or b'').decode() + (exc.stderr or b'').decode()
            label = name + ('-' + case if case else '')
            (out / (label + '-run.log')).write_text(output)
            if name == 'fixed':
                assert not timeout and rc == 0, (label, output)
            elif name == 'legacy':
                assert timeout, (label, rc, output)
            else:
                assert not timeout and rc != 0 and 'assert' in output.lower(), (label, rc, output)
            results.append(dict(case=label, timed_out=timeout, returncode=rc,
                                output=output.strip()))
    assert before == {str(path):sha(path) for path in inputs}, 'Inputs changed'
    receipt = dict(status='PASS_PRODUCTION_USB_COMMON_BOUNDARIES', hardware=False,
                   generated_sha256=hashlib.sha256(common.encode()).hexdigest(),
                   exact_function_sha256={name:hashlib.sha256(body.encode()).hexdigest()
                                          for name,body in fixed_functions.items()},
                   source_hashes=before, results=results,
                   limits=['Synthetic W1C/PHY/DMA boundaries; no physical failure or recovery claim.',
                           'VBUS-removal matrix permits the still-powered digital peripheral to finish DMA; missing-completion cases separately retain failure/ownership.',
                           'Native pointer-width casts only map MCU DMA address payloads; all ordering branches retain generated bodies.',
                           'No hardware, remote wake, USB retry loop or retained radio role is introduced.'])
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS exact common USB lifecycle/ownership and six negative controls; no hardware')


if __name__ == '__main__':
    main()
