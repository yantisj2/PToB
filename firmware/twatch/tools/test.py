#!/usr/bin/env python3
"""Host behavior checks and native-renderer PPM previews; no hardware or enrollment.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,subprocess,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
app=Path(__file__).resolve().parents[1];sources=[app/'main/watch.cpp',app/'main/watch.hpp',app/'main/font.hpp',app/'tests/watch_test.cpp',app/'tools/preview.cpp',app/'main/bootrom.hpp',app/'tests/bootrom_test.cpp',app/'tests/continuity_test.cpp']
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),'-I'+str(app.parent/'w12/main'),str(app/'tests/bootrom_test.cpp'),'-o',str(a.out/'bootrom-test')],check=True,timeout=60)
subprocess.run([str(a.out/'bootrom-test')],check=True,timeout=10)
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'main/watch.cpp'),str(app/'tests/continuity_test.cpp'),'-o',str(a.out/'continuity-test')],check=True,timeout=60)
subprocess.run([str(a.out/'continuity-test')],check=True,timeout=10)
base=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'main/watch.cpp')]
subprocess.run(base+[str(app/'tests/watch_test.cpp'),'-o',str(a.out/'test')],check=True,timeout=60);subprocess.run([str(a.out/'test')],check=True,timeout=10)
subprocess.run(base+[str(app/'tools/preview.cpp'),'-o',str(a.out/'render')],check=True,timeout=60)
for state in ['face','launcher','settings','faces','waiting','acquire','lora','hold','maximum','save-error','sleep','instrument','instrument-wait','instrument-hold','instrument-over','instrument-rc9','instrument-lora','instrument-acquire','instrument-noon']:subprocess.run([str(a.out/'render'),state,str(a.out/(state+'.ppm'))],check=True,timeout=10)
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'HOST_UI_ONLY','fixtures':'SYNTHETIC_NOT_HARDWARE','sources':{str(q.relative_to(app)):hashlib.sha256(q.read_bytes()).hexdigest() for q in sources}},indent=2)+'\n')
print('PASS: navigation, settings bounds, wake consumption, expiry, framebuffer bounds and 19 native previews; analog selection, 90s sleep and display-only clock continuity')
