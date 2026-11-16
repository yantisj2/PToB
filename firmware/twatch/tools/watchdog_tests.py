#!/usr/bin/env python3
"""Watch radio timers and persistent journal. Offline; no device access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
sources=[APP/'main/fault_watchdog.cpp',APP/'main/radio_watchdog.hpp',APP/'main/radio_diagnostics.hpp',Path(__file__)]
for name in ('radio_watchdog','radio_journal','radio_diagnostics'):
 source=APP/'tests'/f'{name}_test.cpp';sources.append(source)
 exe=a.out/name
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-DESP_PLATFORM',
                 '-I'+str(APP/'tests/watchdog_stubs'),'-I'+str(APP/'main'),'-I'+str(APP.parent/'w12/main'),str(source),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
sources+=sorted((APP/'tests/watchdog_stubs').rglob('*.h'))
(a.out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='HOST_FAULT_TIMERS_ACTUAL_JOURNAL_FAKE_NVS_NO_HARDWARE',sources={str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}),indent=2)+'\n')
print('PASS independent 90-second timers, OFF suppression, bounded flash ring, reload, storage errors, no loss-of-time reset')
