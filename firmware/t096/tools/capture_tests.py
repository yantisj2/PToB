#!/usr/bin/env python3
"""Finite native capture regressions at public driver boundary; no hardware."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();app=Path(__file__).resolve().parents[1];out=a.out.resolve();assert not out.exists();out.mkdir();src=out/'source';shutil.copytree(app,src,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for n in ['nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h','hal/nrf_radio.h','zephyr/drivers/clock_control/nrf_clock_control.h','zephyr/drivers/gpio.h','zephyr/sys/atomic.h','zephyr/sys/onoff.h']:
 f=out/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* public boundary supplied by capture_fake.h */\n')
cmd=[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(src/'include'),'-I'+str(out),'-include',str(src/'tests/capture_fake.h'),str(src/'tests/capture_test.c'),'-o',str(out/'capture')]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);results=[]
if r.returncode==0:
 for mode in range(15):
  r=subprocess.run([str(out/'capture'),str(mode)],capture_output=True,text=True,timeout=10);(out/f'mode{mode}.log').write_text(r.stdout+r.stderr);results.append({'mode':mode,'exit_code':r.returncode})
receipt={'status':'PASS' if len(results)==15 and all(x['exit_code']==0 for x in results) else 'FAIL','cases':results,'files':{str(f.relative_to(src)):hashlib.sha256(f.read_bytes()).hexdigest() for f in src.rglob('*') if f.is_file()}}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'out':str(out),'status':receipt['status'],'cases':results}));raise SystemExit(receipt['status']!='PASS')
