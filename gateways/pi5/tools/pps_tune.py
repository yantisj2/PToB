#!/usr/bin/env python3
"""Reduce Pi PPS capture contention. Copyright 2026 Jonathan Yantis. MIT."""
import json
from pathlib import Path
import re
import os
import subprocess


def install(params):
    if os.geteuid(): raise ValueError('root required')
    for name, value in params['files'].items():
        if name == 'script': path=Path('/opt/ptob/pi5/pps_tune.py')
        elif name == 'unit': path=Path('/etc/systemd/system/ptob-pps-tuning.service')
        else: raise ValueError('unexpected tuning file')
        if path.is_symlink(): raise ValueError('linked output')
        path.write_text(value); path.chmod(0o644)
    config=Path('/etc/ptob/pi5/pps-tuning.json')
    config.write_text(json.dumps(params['binding'])+'\n');config.chmod(0o600)
    for argv in (['systemd-analyze','verify','/etc/systemd/system/ptob-pps-tuning.service'],
                 ['systemctl','daemon-reload'],['systemctl','enable','ptob-pps-tuning'],
                 ['systemctl','restart','ptob-pps-tuning']):
        subprocess.run(argv,check=True,capture_output=True,timeout=30)
    return json.loads(Path('/run/ptob-pps-tuning.json').read_text())


def apply():
    binding = json.loads(Path('/etc/ptob/pi5/pps-tuning.json').read_text())
    if Path('/proc/device-tree/serial-number').read_text().strip('\x00\n') != binding['serial']:
        raise ValueError('exact Pi serial mismatch')
    if Path('/opt/ptob/pi5/IMAGE_ID').read_text().strip() != binding['image_id']:
        raise ValueError('boot image mismatch')
    pps = Path('/dev/pps-ptob').resolve(strict=True).name
    if not (Path('/sys/class/pps')/pps/'name').read_text().strip().startswith('ptob-pps'):
        raise ValueError('wrong PPS device')
    lines=[line for line in Path('/proc/interrupts').read_text().splitlines()
           if re.search(r'\bpinctrl-rp1\s+16\s+Edge\s+ptob-pps',line)]
    if len(lines)!=1:
        raise ValueError('require one GPIO16 PPS IRQ')
    irq=int(lines[0].split(':')[0])
    affinity=Path('/proc/irq')/str(irq)/'smp_affinity_list'
    before=affinity.read_text().strip()
    affinity.write_text('3\n')
    effective=(affinity.parent/'effective_affinity_list').read_text().strip()
    if effective!='3':
        affinity.write_text(before+'\n')
        raise ValueError('PPS IRQ affinity did not take effect')
    # Prevent ondemand frequency transitions on the host capture path.
    governor=Path('/sys/devices/system/cpu/cpufreq/policy0/scaling_governor')
    prior=governor.read_text().strip();governor.write_text('performance\n')
    out=dict(irq=irq,irq_before=before,irq_effective=effective,
             governor_before=prior,governor=governor.read_text().strip())
    Path('/run/ptob-pps-tuning.json').write_text(json.dumps(out)+'\n')
    return out


if __name__=='__main__':
    print(json.dumps(install(PARAMS) if 'PARAMS' in globals() else apply()))
