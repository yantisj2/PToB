#!/usr/bin/env python3
"""Acquire only pinned public M5 display dependencies, never firmware or USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dest',type=Path,required=True);a=p.parse_args()
a.dest.mkdir(parents=True,exist_ok=False)
pins=json.loads((APP/'dependencies.lock.json').read_text())
for name in ('M5GFX','M5Unified'):
 pin=pins[name];dest=a.dest/name
 subprocess.run(['git','init','--quiet',str(dest)],check=True)
 subprocess.run(['git','-C',str(dest),'remote','add','origin',pin['repository']],check=True)
 subprocess.run(['git','-C',str(dest),'fetch','--depth','1','origin',pin['revision']],check=True)
 subprocess.run(['git','-C',str(dest),'checkout','--quiet','--detach','FETCH_HEAD'],check=True)
 revision=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
 if revision!=pin['revision']:raise ValueError(name+' revision mismatch')
 print(name,revision)
