#!/usr/bin/env python3
"""Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
Actual fault producer/boot paths at synthetic ARM/reset boundaries; no hardware.
"""
import argparse,copy,hashlib,importlib.util,json,os,subprocess
from pathlib import Path

APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 files=[Path(__file__),*[APP/q for q in ('src/watchdog.c','src/watchdog_policy.c','src/health.c',
  'src/diagnostic.c','include/health.h','include/crash_record.h','include/watchdog_policy.h',
  'include/watchdog.h','include/snapshot_witness.h','tools/inspect_build.py','tools/crash_decode.py',
  'tests/watchdog_test.c','tests/health_fake.h','tests/zephyr_fake.h','tests/crash_capture_test.c',
  'tests/fault_retention_watchdog_test.c','tests/fault_retention_crash_test.c')]]
 bound={str(f.relative_to(APP)):sha(f) for f in files};passed=[]
 for n in ('zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/device.h',
   'zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','zephyr/fatal.h','zephyr/irq.h',
   'zephyr/timing/timing.h','cmsis_core.h','build_identity.h','ptob_device.h'):
  f=out/n;f.parent.mkdir(parents=True,exist_ok=True)
  f.write_text('#define PTOB_DEVICE_ID "123456789abcdef0"\n' if n=='ptob_device.h' else '/* synthetic public API boundary */\n')
 cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else'clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
  '-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
  '-I'+str(out),'-I'+str(APP/'include'),'-I'+str(APP/'protocol')]
 def run(label,cmd):
  with (out/(label+'.log')).open('x') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
  passed.append(label)
 try:
  for case,extra in [('fault_retention_watchdog_test',[APP/'src/watchdog_policy.c']),
                     ('fault_retention_crash_test',[]),('crash_capture_test',[])]:
   binary=out/case
   run(case+'-compile',[*flags,str(APP/'tests'/(case+'.c')),*map(str,extra),'-o',str(binary)])
   run(case,[str(binary),*([str(out/'crash.bin')]if case=='crash_capture_test'else[])])
  spec=importlib.util.spec_from_file_location('inspection',APP/'tools/inspect_build.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  symbols={name:[dict(size=size,section=section,type='SHT_NOBITS',address=0x20020000+i*1024)]
   for i,(name,size,section) in enumerate([('r2_watchdog_capsule',136,'noinit'),('r2_crash_record',512,'noinit'),('r2_fault_capsule',256,'bss'),('uf2_intent',88,'noinit')])}
  descriptor=m.fault_only_retention(True,(0,15,308),symbols);assert descriptor['bytes']==648 and not descriptor['operational_restore'] and not descriptor['flash_persistence']
  assert m.fault_only_retention(True,(0,15,307),symbols)is None and m.fault_only_retention(False,(0,15,308),symbols)is None
  rejects=0
  for name in symbols:
   for key,value in [('size',symbols[name][0]['size']+4),('section','data'),('type','SHT_PROGBITS'),('address',0x20040000)]:
    bad=copy.deepcopy(symbols);bad[name][0][key]=value
    try:m.fault_only_retention(True,(0,15,308),bad)
    except ValueError:rejects+=1
    else:raise AssertionError((name,key))
   for value in ([],symbols[name]*2):
    bad=copy.deepcopy(symbols);bad[name]=value
    try:m.fault_only_retention(True,(0,15,308),bad)
    except ValueError:rejects+=1
    else:raise AssertionError(name)
  assert rejects==24;passed.append('native-exact-fault-only-allocation-contract-and24-negatives')
  assert all(sha(APP/f)==h for f,h in bound.items())
 finally:
  (out/'receipt.json').write_text(json.dumps(dict(status='PASS'if len(passed)==7 else 'INCOMPLETE',
   cases=passed,hardware=False,flash_backend_linked=False,inputs=bound,
   descriptor=locals().get('descriptor'),limits=['Synthetic reset retains host arrays; actual SRAM survival and native lifecycle require separate proof.',
    'No physical watchdog/CPU exception or accuracy claim. No historical flash-writer tests run; production diagnostics remain RAM-only.']),indent=2)+'\n')
 print('PASS fault-only retention actual producers/boot + unchanged bounded CPU capture + exact linked allocation contract')
if __name__=='__main__':main()
