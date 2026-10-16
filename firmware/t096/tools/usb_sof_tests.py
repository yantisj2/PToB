#!/usr/bin/env python3
"""ASan/UBSan capture-owner and raw USB worker tests, no hardware access."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir()
headers=['nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h','hal/nrf_radio.h','hal/nrf_usbd.h','zephyr/drivers/clock_control/nrf_clock_control.h','zephyr/drivers/gpio.h','zephyr/sys/atomic.h','zephyr/sys/onoff.h','zephyr/usb/usb_device.h','zephyr/kernel.h','zephyr/sys/byteorder.h']
for name in headers:
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* public boundary supplied by fake */\n')
cc=(__import__('os').environ.get('CC') or __import__('shutil').which('clang'));common=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(app/'include'),'-I'+str(out)]
cases=[]
def run(name,cmd):
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=20);(out/(name+'.log')).write_text(r.stdout+r.stderr);cases.append({'name':name,'exit_code':r.returncode});return r.returncode==0
if run('capture-build',common+['-include',str(app/'tests/usb_capture_fake.h'),str(app/'tests/usb_capture_test.c'),str(app/'src/usb_sof_observer.c'),'-o',str(out/'capture')]):
 for mode in (0,15,16,17,18,19):run('capture-'+str(mode),[str(out/'capture'),str(mode)])
if run('worker-build',common+['-include',str(app/'tests/usb_sof_probe_fake.h'),str(app/'tests/usb_sof_probe_test.c'),str(app/'src/usb_sof_observer.c'),'-o',str(out/'worker')]):run('worker',[str(out/'worker')])
if run('source-build',common+['-DR2_USB_SOURCE_STREAM=1','-include',str(app/'tests/usb_sof_probe_fake.h'),str(app/'tests/usb_source_test.c'),'-o',str(out/'source')]):run('source',[str(out/'source')])
r={'status':'PASS' if len(cases)==11 and all(x['exit_code']==0 for x in cases) else 'FAIL','hardware_access':False,'cases':cases}
(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));raise SystemExit(r['status']!='PASS')
