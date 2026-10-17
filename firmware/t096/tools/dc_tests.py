#!/usr/bin/env python3
"""Actual DC source publisher/USB serialization and conversion, ASan/UBSan; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True)
for name in ['zephyr/kernel.h','zephyr/usb/usb_device.h','zephyr/sys/byteorder.h','zephyr/drivers/clock_control.h',
             'zephyr/drivers/clock_control/nrf_clock_control.h','zephyr/sys/onoff.h']:
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Test boundary supplied by fake. */\n')
source=(app/'src/rc_boundary.c').read_text()
arithmetic=source[source.index('static bool map_value('):source.index('bool rc5_clock_discipline(')]
arithmetic+=source[source.index('bool rc5_map_project('):source.index('bool rc5_clock_capture(')]
(out/'dc_projection_under_test.h').write_text(arithmetic)
cc=os.environ.get('CC') or shutil.which('clang')
flags=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-g','-I'+str(app/'include'),'-I'+str(out)]
results=[]
for name,extra in [('dc_clock',[]),('dc_chain',['-DR2_DC_PROFILE=1','-include',str(app/'tests/usb_sof_probe_fake.h'),'-include',str(app/'tests/dc_time_fake.h')])]:
 command=[*flags,*extra,str(app/'tests'/(name+'_test.c')),str(app/'src/dc_clock.c'),str(app/'src/precision.c'),
          str(app/'src/clock.c'),str(app/'src/nmea.c'),'-o',str(out/name)]
 subprocess.run(command,check=True,timeout=30)
 result=subprocess.run([str(out/name)],timeout=30,capture_output=True,text=True)
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 result.check_returncode()
 print(result.stdout,end='');results.append({'name':name,'status':'PASS','output':result.stdout.strip()})
subprocess.run([*flags,'-DR2_DC_PROFILE=1',str(app/'tests/dc_ui_test.c'),str(app/'src/ui_render.c'),
 str(app/'src/precision.c'),'-o',str(out/'dc_ui')],check=True,timeout=30)
result=subprocess.run([str(out/'dc_ui'),str(out)],check=True,timeout=30,capture_output=True,text=True)
print(result.stdout,end='');results.append({'name':'dc_ui','status':'PASS','output':result.stdout.strip()})
subprocess.run([*flags,'-Wno-unused-function','-DR2_DC_PROFILE=1','-include',str(app/'tests/zephyr_fake.h'),
 str(app/'tests/dc_platform_test.c'),'-o',str(out/'dc_platform')],check=True,timeout=30)
for mode in range(4):
 result=subprocess.run([str(out/'dc_platform'),str(mode)],check=True,timeout=30,capture_output=True,text=True)
 print(result.stdout,end='');results.append({'name':'dc_platform_'+str(mode),'status':'PASS'})
subprocess.run([*flags,'-DR2_DC_PROFILE=1','-include',str(app/'tests/usb_sof_probe_fake.h'),
 str(app/'tests/dc_usb_lifecycle_test.c'),str(app/'src/usb_sof_observer.c'),'-o',str(out/'dc_usb_lifecycle')],check=True,timeout=30)
result=subprocess.run([str(out/'dc_usb_lifecycle')],check=True,timeout=30,capture_output=True,text=True)
print(result.stdout,end='');results.append({'name':'dc_usb_lifecycle','status':'PASS'})
(out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'tests':results},indent=2)+'\n')
