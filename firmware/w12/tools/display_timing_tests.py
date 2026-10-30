#!/usr/bin/env python3
"""UTC second-boundary paints and independent RC9/source selection; no USB."""
import os
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
for path in (APP/'main',APP/'main/r2/r2-g1',APP/'main/r2/rc7/include'):flags+=['-I'+str(path)]
paths=[APP/'main/display_second_deadline.hpp',APP/'main/rc9_source_policy.hpp',APP/'main/rc9_time.hpp',APP/'main/sync_watchdog.hpp',Path(__file__)]
for name in ('display_second_deadline','rc9_source_policy','rc9_time'):
 src=APP/'tests'/(name+'_test.cpp');paths.append(src)
 subprocess.run([os.environ.get('CXX','clang++'),*flags,src,'-o',out/name],check=True)
 subprocess.run([out/name],check=True)
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,scope='UTC_BOUNDARY_PAGE_CHANGE_DELAYED_PAINT_SOURCE_ROLLBACK_OVERFLOW_RC9_FRESHNESS_SOURCE_LEASE_ASAN_UBSAN',sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}),indent=2)+'\n')
print('PASS UTC second-boundary, RC9 time freshness and original source lease selection')
