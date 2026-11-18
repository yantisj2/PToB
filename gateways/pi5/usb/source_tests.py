#!/usr/bin/env python3
"""Offline unchanged T096 source/race + deterministic frame boundary checks.

Copyright (c) 2026 Jonathan Yantis. MIT. No hardware access or firmware build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[3]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True,help='new ignored artifacts/ directory')
    p.add_argument('--sanitizer',choices=('undefined','address,undefined','none'),default='undefined')
    args=p.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/'artifacts'):raise ValueError('output must be ignored artifacts/')
    out.parent.mkdir(parents=True,exist_ok=True);out.mkdir(mode=0o700);app=ROOT/'firmware/t096';cases=[]
    for name in ('nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h',
                 'hal/nrf_radio.h','hal/nrf_usbd.h','zephyr/drivers/clock_control/nrf_clock_control.h',
                 'zephyr/drivers/gpio.h','zephyr/sys/atomic.h','zephyr/sys/onoff.h',
                 'zephyr/usb/usb_device.h','zephyr/kernel.h','zephyr/sys/byteorder.h'):
        f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* supplied by public fake */\n')
    compiler=os.environ.get('CC') or shutil.which('clang')
    if not compiler:raise RuntimeError('clang or explicit CC required')
    flags=[] if args.sanitizer=='none' else ['-fsanitize='+args.sanitizer,'-fno-sanitize-recover=all']
    common=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,
            '-I'+str(app/'include'),'-I'+str(out)]
    inputs=[app/'src/usb_source.c',app/'tests/usb_source_test.c',app/'tests/usb_source_race_test.c',
            app/'tests/usb_sof_probe_fake.h',app/'tests/usb_capture_fake.h',app/'tests/usb_capture_test.c',
            app/'tests/usb_sof_probe_test.c',app/'src/capture.c',app/'src/usb_sof_observer.c',
            Path(__file__),Path(__file__).with_name('tests')/'source_boundary.c']
    groups=[('capture',app/'tests/usb_capture_test.c','usb_capture_fake.h',[app/'src/usb_sof_observer.c'],[(str(i),[str(i)]) for i in (0,15,16,17,18,19)]),
            ('worker',app/'tests/usb_sof_probe_test.c','usb_sof_probe_fake.h',[app/'src/usb_sof_observer.c'],[('run',[])]),
            ('source',inputs[1],'usb_sof_probe_fake.h',[],[('run',[])]),
            ('race',inputs[2],'usb_sof_probe_fake.h',[],[('run',[])]),
            ('boundary',inputs[-1],'usb_sof_probe_fake.h',[],[('run',[])])]
    for name,source,fake,extra,runs in groups:
        build=common+['-DR2_USB_SOURCE_STREAM=1','-include',str(app/'tests'/fake),str(source),*map(str,extra),'-o',str(out/name)]
        for phase,cmd in [('build',build)]+[(phase,[str(out/name),*argv]) for phase,argv in runs]:
            case=dict(name=name,phase=phase,command=cmd)
            try:
                r=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
                case['exit_code']=r.returncode;log=r.stdout+r.stderr
            except subprocess.TimeoutExpired as e:
                def decoded(value):return value.decode() if isinstance(value,bytes) else value or ''
                case['timeout_seconds']=20;log=decoded(e.stdout)+decoded(e.stderr)
            (out/(name+'-'+phase+'.log')).write_text(log);cases.append(case)
            if case.get('exit_code')!=0:break
    receipt=dict(status='PASS' if len(cases)==15 and all(c.get('exit_code')==0 for c in cases) else 'FAIL',
                 hardware_access=False,sanitizer=args.sanitizer,cases=cases,
                 inputs={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},
                 boundary_meaning='Existing unchanged source accepts stable frame/CC mismatch snapshots under age16000; fixture does not establish actual hardware ordering.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt));raise SystemExit(receipt['status']!='PASS')


if __name__=='__main__':main()
