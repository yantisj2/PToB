#!/usr/bin/env python3
"""Actual generated host-frame suspend recovery, with explicit fake MMIO boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from usb_common_boundary_tests import function
from usb_ep0_recovery import generate

APP = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--deps', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    generate(a.deps / 'zephyr', out / 'common.c', 'usb-dma-health')
    body = (out / 'common.c').read_text()
    fixture = APP / 'tests/usb_host_resume_fixture.cpp'
    names = ('nrf_usbd_common_start', 'nrf_usbd_common_suspend', 'r2_usb_host_activity',
             'ev_usbevent_handler', 'nrf_usbd_common_irq_handler')
    functions = {name: function(body, name) for name in names}
    variants = {'fixed': functions.copy(), 'fixed-explicit-off': functions.copy()}
    old = functions.copy()
    old['nrf_usbd_common_irq_handler'] = old['nrf_usbd_common_irq_handler'].replace(
        'r2_usb_host_activity();', '(void)0;')
    variants['negative-no-host-wake'] = old
    for label, name, before, after in (
        ('start-sof-policy', 'nrf_usbd_common_start', 'r2_sof_requested = enable_sof;', '(void)0;'),
        ('start-pending-reset', 'nrf_usbd_common_start', 'r2_host_resume_pending = false;', '(void)0;'),
        ('stale-frame', 'nrf_usbd_common_suspend', 'NRF_USBD->EVENTS_SOF = 0;', '(void)0;'),
        ('no-sof-irq', 'nrf_usbd_common_suspend', 'NRF_USBD->INTENSET = USBD_INTEN_SOF_Msk;', '(void)0;'),
        ('early-endpoints', 'nrf_usbd_common_irq_handler',
         'if (r2_host_resume_pending) { NRF_USBD->EVENTS_SOF = 0; return; }', '(void)0;'),
        ('lost-mask', 'r2_usb_host_activity', 'NRF_USBD->INTENSET = r2_host_resume_inten;', '(void)0;'),
        ('new-suspend', 'r2_usb_host_activity',
         'NRF_USBD->EVENTS_SOF = 0; /* Pre-idle frames cannot undo this new suspend. */', '(void)0;'),
        ('remote-wake', 'r2_usb_host_activity', 'r2_wakeup_requested = false;', 'r2_wakeup_requested = true;'),
        ('late-event-ack', 'r2_usb_host_activity',
         'NRF_USBD->EVENTS_USBEVENT = 0;\n  uint32_t event = NRF_USBD->EVENTCAUSE;',
         'uint32_t event = NRF_USBD->EVENTCAUSE;\n  NRF_USBD->EVENTS_USBEVENT = 0;'),
        ('fault-irq-storm', 'r2_usb_host_activity',
         'if (r2_host_resume_pending) NRF_USBD->INTENCLR = NRF_USBD->INTEN;', '(void)0;'),
    ):
        v = functions.copy()
        assert before in v[name], label
        v[name] = v[name].replace(before, after)
        variants['negative-' + label] = v
    results = []
    cc = os.environ.get('CXX') or (
        '/opt/homebrew/opt/llvm/bin/clang++'
        if Path('/opt/homebrew/opt/llvm/bin/clang++').is_file()
        else shutil.which('clang++'))
    assert cc, 'C++ compiler required for W1C register boundaries'
    for name, bodies in variants.items():
        code = fixture.read_text().replace('/* PTOB_USB_HOST_RESUME_BODIES */',
                                          '\n\n'.join(bodies.values()))
        source, binary = out / (name + '.cpp'), out / name
        source.write_text(code)
        built = subprocess.run([cc, '-std=c++17', '-O1', '-g',
                                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                '-I' + str(a.deps / 'modules/hal/nordic/nrfx/mdk'),
                                *(['-DR2_USB_AWAKE_CONTROL=0'] if name == 'fixed-explicit-off' else []),
                                str(source), '-o', str(binary)], capture_output=True, text=True)
        (out / (name + '-build.log')).write_text(built.stdout + built.stderr)
        built.check_returncode()
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
        output = run.stdout + run.stderr
        (out / (name + '-run.log')).write_text(output)
        assert (run.returncode == 0) if name.startswith('fixed') else (run.returncode != 0 and 'assert' in output.lower()), (name, output)
        results.append(dict(variant=name, exit_code=run.returncode))
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = ('cmake/usb-dma-health.patch', 'cmake/usb-dma-health.json',
              'tests/usb_host_resume_fixture.cpp', 'tools/usb_host_resume_tests.py')
    (out / 'receipt.json').write_text(json.dumps(dict(
        status='PASS_HOST_SOF_RESUME_BOUNDARIES_OFFLINE', hardware=False,
        generated_sha256=hashlib.sha256(body.encode()).hexdigest(),
        sdk_bitfields_sha256=sha(a.deps / 'modules/hal/nordic/nrfx/mdk/nrf52840_bitfields.h'),
        inputs={n: sha(APP / n) for n in inputs}, results=results,
        limits='Actual production bodies; explicit SOF, PHY-ready and MMIO boundaries. '
               'No reproduction or qualification of the physical outage.'), indent=2) + '\n')
    print('PASS actual host-frame resume (default and explicit-off) and eleven causal negative controls; no hardware')


if __name__ == '__main__':
    main()
