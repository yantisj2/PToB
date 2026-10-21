#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Actual health writer, offline."""
import argparse
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
for name in ['zephyr/fatal.h','zephyr/irq.h','zephyr/kernel.h','zephyr/timing/timing.h','cmsis_core.h','build_identity.h']:
    f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Native boundary supplied by fixture. */\n')
flags=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
for name,extra in [('health',[]),('watchdog-health',['-DR2_WATCHDOG=1']),('controller-health',['-DR2_WATCHDOG=1','-DR2_CONTROLLER_DIAGNOSTICS=1'])]:
    subprocess.run([*flags,*extra,str(app/'tests/health_test.c'),'-o',str(out/name)],check=True,timeout=30)
    subprocess.run([str(out/name)],check=True,timeout=10)
