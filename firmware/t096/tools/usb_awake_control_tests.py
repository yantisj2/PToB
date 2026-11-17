#!/usr/bin/env python3
"""Actual generated PHY-awake diagnostic suspend control, with explicit fake MMIO boundaries.
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
    fixture = APP / 'tests/usb_awake_control_fixture.cpp'
    names = ('nrf_usbd_common_start', 'nrf_usbd_common_suspend', 'r2_usb_host_activity',
             'ev_usbevent_handler', 'nrf_usbd_common_irq_handler')
    functions = {name: function(body, name) for name in names}
    variants = {'fixed': functions.copy()}
    for label, name, before, after in (
        ('phy-sleep', 'nrf_usbd_common_suspend',
         '#if defined(R2_USB_AWAKE_CONTROL) && R2_USB_AWAKE_CONTROL', '#if 0'),
        ('lost-suspend', 'ev_usbevent_handler', 'm_bus_suspend = true;', 'm_bus_suspend = false;'),
        ('invented-resume', 'r2_usb_host_activity',
         'if (m_bus_suspend && NRF_USBD->EVENTS_SOF)', 'if (m_bus_suspend)'),
        ('dma-ownership', 'nrf_usbd_common_suspend', '!r2_usb_dma_take()', 'false'),
        ('no-host-resume', 'nrf_usbd_common_irq_handler', 'r2_usb_host_activity();', '(void)0;'),
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
                                '-DR2_USB_AWAKE_CONTROL=1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                '-I' + str(a.deps / 'modules/hal/nordic/nrfx/mdk'),
                                str(source), '-o', str(binary)], capture_output=True, text=True)
        (out / (name + '-build.log')).write_text(built.stdout + built.stderr)
        built.check_returncode()
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
        output = run.stdout + run.stderr
        (out / (name + '-run.log')).write_text(output)
        assert (run.returncode == 0) if name == 'fixed' else (run.returncode != 0 and 'assert' in output.lower()), (name, output)
        results.append(dict(variant=name, exit_code=run.returncode))
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    inputs = ('cmake/usb-dma-health.patch', 'cmake/usb-dma-health.json',
              'tests/usb_awake_control_fixture.cpp', 'tools/usb_awake_control_tests.py')
    (out / 'receipt.json').write_text(json.dumps(dict(
        status='PASS_AWAKE_CONTROL_BOUNDARIES_OFFLINE', hardware=False,
        generated_sha256=hashlib.sha256(body.encode()).hexdigest(),
        sdk_bitfields_sha256=sha(a.deps / 'modules/hal/nordic/nrfx/mdk/nrf52840_bitfields.h'),
        inputs={n: sha(APP / n) for n in inputs}, results=results,
        limits='Actual production bodies; explicit SOF, PHY-ready and MMIO boundaries. '
               'No reproduction or qualification of the physical outage.'), indent=2) + '\n')
    print('PASS actual awake-control suspend, fresh-frame resume and five negative controls; no hardware')


if __name__ == '__main__':
    main()
