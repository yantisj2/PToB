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
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'main/watch.cpp'),str(app/'main/calendar.cpp'),str(app/'tests/continuity_test.cpp'),'-o',str(a.out/'continuity-test')],check=True,timeout=60)
subprocess.run([str(a.out/'continuity-test')],check=True,timeout=10)
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(app/'main'),'-I'+str(app.parent/'w12/main'),str(app/'tests/watch_button_test.cpp'),'-o',str(a.out/'button-test')],check=True,timeout=60)
subprocess.run([str(a.out/'button-test')],check=True,timeout=10)
sources += [app/'main/watch_button.hpp',app/'tests/watch_button_test.cpp']
mobile=['c++','-std=c++20','-DPTOB_N30=1','-Wall','-Wextra','-Werror',*['-I'+str(app.parent/q) for q in ['twatch/main','n30/main','w12/main','w12/legacy','w12/main/r2/r2-g1','w12/main/r2/rc7/include']],str(app/'tests/mobile_time_test.cpp'),str(app.parent/'n30/main/ui_model.cpp'),'-o',str(a.out/'mobile-test')]
subprocess.run(mobile,check=True,timeout=60);subprocess.run([str(a.out/'mobile-test')],check=True,timeout=10)
sources += [app/'main/mobile_time.hpp',app/'main/radio_candidates.hpp',app/'tests/mobile_time_test.cpp']
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'tests/wrist_motion_test.cpp'),'-o',str(a.out/'wrist-test')],check=True,timeout=60);subprocess.run([str(a.out/'wrist-test')],check=True,timeout=10)
sources += [app/'main/wrist_motion.hpp',app/'tests/wrist_motion_test.cpp']
imu=['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined',*['-I'+str(app/q) for q in ['tests/imu_stubs','main','vendor/bhi260']],str(app/'main/imu.cpp'),str(app/'tests/imu_adapter_test.cpp'),'-o',str(a.out/'imu-test')]
subprocess.run(imu,check=True,timeout=60);subprocess.run([str(a.out/'imu-test')],check=True,timeout=30)
subprocess.run(['python3',str(app/'tests/imu_report_test.py')],check=True,timeout=10)
sources += [app/'tools/imu_report.py',app/'tests/imu_report_test.py']
subprocess.run(['python3',str(app/'tests/receiver_report_test.py')],check=True,timeout=10)
sources += [app/'tools/receiver_report.py',app/'tests/receiver_report_test.py']
sources += [app/'main/imu.cpp',app/'main/imu.hpp',app/'tests/imu_adapter_test.cpp',*sorted((app/'tests/imu_stubs').rglob('*.h'))]
base=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'main/watch.cpp'),str(app/'main/calendar.cpp')]
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(app/'main'),str(app/'main/calendar.cpp'),str(app/'tests/solar_geometry_test.cpp'),'-o',str(a.out/'solar-geometry-test')],check=True,timeout=60)
subprocess.run([str(a.out/'solar-geometry-test')],check=True,timeout=10)
sources += [app/'main/solar_geometry.hpp',app/'tests/solar_geometry_test.cpp']
subprocess.run(base+[str(app/'tests/touch_capture_test.cpp'),'-fsanitize=undefined','-o',str(a.out/'touch-test')],check=True,timeout=60);subprocess.run([str(a.out/'touch-test')],check=True,timeout=10)
sources += [app/'main/touch_capture.hpp',app/'tests/touch_capture_test.cpp']
subprocess.run(base+[str(app/'tests/render_damage_test.cpp'),'-o',str(a.out/'damage-test')],check=True,timeout=60);subprocess.run([str(a.out/'damage-test')],check=True,timeout=30)
sources += [app/'tests/render_damage_test.cpp',app/'main/calendar.hpp',app/'main/calendar.cpp']
subprocess.run(base+[str(app/'tests/watch_test.cpp'),'-o',str(a.out/'test')],check=True,timeout=60);subprocess.run([str(a.out/'test')],check=True,timeout=10)
subprocess.run(base+[str(app/'tools/preview.cpp'),'-o',str(a.out/'render')],check=True,timeout=60)
for state in ['face','launcher','settings','faces','waiting','acquire','lora','hold','maximum','save-error','sleep','instrument','instrument-wait','instrument-hold','instrument-over','instrument-rc9','instrument-lora','instrument-acquire','instrument-noon','solar','solar-dawn','solar-sunset','solar-night','solar-new','solar-full','solar-expired','solar-wait','solar-hold','solar-over','solar-midnight','solar-morning','solar-afternoon','solar-civil','solar-nautical','solar-astronomical','solar-summer','solar-winter','solar-spring','solar-autumn','solar-dst-start-before','solar-dst-start-after','solar-dst-end-before','solar-dst-end-after']+[f'solar-month-{m:02d}' for m in range(1,13)]:subprocess.run([str(a.out/'render'),state,str(a.out/(state+'.ppm'))],check=True,timeout=10)
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'HOST_UI_ONLY','fixtures':'SYNTHETIC_NOT_HARDWARE','sources':{str(q.relative_to(app)):hashlib.sha256(q.read_bytes()).hexdigest() for q in sources}},indent=2)+'\n')
print('PASS: navigation, settings bounds, wake consumption, expiry, framebuffer bounds and 55 native previews; analog selection, 15/30/60/90s sleep and display-only clock continuity')
