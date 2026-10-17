#!/usr/bin/env python3
"""Production commit/publication regression with ASan/UBSan; no hardware."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--omit-immediate-publication',action='store_true',help='Negative fixture reproducing former loop-end-only policy');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False);src=app/'src/rc_dynamic_service.c';text=src.read_text()
def function(name,typ='void'):
 start=text.index('static '+typ+' '+name+'(');end=text.index('{',start)+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
bodies='\n'.join(function(n,t) for n,t in [('delivery_snapshot','void'),('delivery_source_poll','void'),('clock_offer','struct r2_uc_clock'),('usb_offer_now','void'),('discipline_commit','void')])
if a.omit_immediate_publication:
 assert bodies.count('\n  usb_offer_now();')==1
 bodies=bodies.replace('\n  usb_offer_now();','\n  /* negative: former loop-end-only publication */')
(out/'rc_publication_under_test.h').write_text(bodies)
for name in ['zephyr/usb/usb_device.h','zephyr/kernel.h','zephyr/sys/byteorder.h']:
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* boundary supplied by fake */\n')
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_USB_SOURCE_STREAM=1','-include',str(app/'tests/usb_sof_probe_fake.h'),'-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(app/'tests/rc_publication_test.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'negative_fixture':a.omit_immediate_publication,'hardware':False,'sources':{str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [src,app/'src/usb_source.c',app/'tests/rc_publication_test.c',Path(__file__).resolve()]},'command':cmd},indent=2)+'\n')
print('PASS production immediateUSB commit ASan/UBSan')
