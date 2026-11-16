#!/usr/bin/env python3
"""Read-only raw USB snapshot and physical System pixels; no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 inputs=[APP/'src/ui_render.c',APP/'src/ui_service.c',APP/'src/ui_critical.inc',APP/'include/usb_health.h',APP/'cmake/usb-ep0-recovery.patch',APP/'cmake/usb-ep0-recovery.json',APP/'tests/usb_debug_ui_test.c',APP/'tools/usb_health_tests.py',Path(__file__)]
 before={str(f):sha(f)for f in inputs};r=dict(status='INCOMPLETE',hardware=False,inputs=before,steps=[])
 def run(name,c):
  r['steps'].append(dict(name=name,command=c))
  with(out/(name+'.log')).open('x')as f:subprocess.run(c,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 try:
  run('snapshot',[sys.executable,str(APP/'tools/usb_health_tests.py'),'--deps',str(a.deps.resolve()),'--out',str(out/'health')])
  cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
  service=(APP/'src/ui_service.c').read_text();start=service.index('  bool saver=!s.menu.open');end=service.index(';',start)+1
  expression=service[start:end].strip().replace('bool saver=','return ',1).replace('r2_ui_saver_allowed(&m,now)','eligible')
  (out/'saver.inc').write_text('static bool actual_saver(unsigned page,uint64_t now,bool eligible,bool menu,bool stable,bool wake_press){struct r2_ui_status s={.page=page,.menu.open=menu};uint64_t idle_since=0;'+expression+'}\n')
  flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_USB_SOURCE_STREAM=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol')]
  run('ui-compile',flags+['-DR2_USB_SAVER_BODY="'+str(out/'saver.inc')+'"',str(APP/'tests/usb_debug_ui_test.c'),str(APP/'src/precision.c'),'-o',str(out/'ui')])
  run('ui',[str(out/'ui'),str(out)])
  # GM7 selects the separate partial-job worker. Exercise its actual entry
  # predicate too: the alternate worker alone cannot prove installed behavior.
  critical=(APP/'src/ui_critical.inc').read_text();start=critical.index('   bool enter=');end=critical.index(';',start)+1
  expression=critical[start:end].strip().replace('bool enter=','return ',1).replace('job.stable','stable').replace('job.wake_press','wake_press')
  (out/'critical-saver.inc').write_text('static bool actual_saver(unsigned page,uint64_t now,bool eligible,bool menu,bool stable,bool wake_press){struct r2_ui_status s={.page=page,.menu.open=menu};uint64_t idle=0;bool allowed=eligible;'+expression+'}\n')
  run('critical-ui-compile',flags+['-DR2_USB_SAVER_BODY="'+str(out/'critical-saver.inc')+'"',str(APP/'tests/usb_debug_ui_test.c'),str(APP/'src/precision.c'),'-o',str(out/'critical-ui')])
  run('critical-ui',[str(out/'critical-ui'),str(out)])
  assert before=={str(f):sha(f)for f in inputs}
  r['status']='PASS_READ_ONLY_USB_SNAPSHOT_AND_SYSTEM_PIXELS'
  r['limits']='Synthetic MMIO/IRQ and production raster boundaries. No hardware, persistent automatic capture, root-cause or recovery claim.'
 finally:
  r['files']={str(f.relative_to(out)):sha(f)for f in out.rglob('*')if f.is_file()and f.name!='receipt.json'}
  (out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
 print(r['status'])
if __name__=='__main__':main()
