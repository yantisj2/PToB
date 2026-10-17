#!/usr/bin/env python3
"""DC-only serial transport bodies, ASan/UBSan; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];a.out.mkdir(parents=True,exist_ok=True)
target=a.out/'dc_gnss_transport'
subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(app/'include'),str(app/'src/dc_gnss_transport.c'),str(app/'tests/dc_gnss_transport_test.c'),'-o',str(target)],check=True)
subprocess.run([str(target)],check=True)
for name in ['zephyr/device.h','zephyr/drivers/gpio.h','zephyr/drivers/uart.h','zephyr/kernel.h','zephyr/sys/atomic.h']:
    f=a.out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Test hardware boundary. */\n')
owner=a.out/'dc_gnss_owner'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-DR2_DC_PROFILE=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(app/'include'),'-I'+str(a.out),'-include',str(app/'tests/gnss_fake.h'),str(app/'tests/dc_gnss_owner_test.c'),'-o',str(owner)],check=True)
for mode in ['reset','timeout','stop']:subprocess.run([str(owner),mode],check=True)
bridge=a.out/'dc_gnss_bridge'
subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(app/'include'),'-I'+str(a.out),str(app/'tests/dc_gnss_bridge_test.c'),str(app/'src/dc_gnss_transport.c'),'-o',str(bridge)],check=True)
subprocess.run([str(bridge)],check=True)
