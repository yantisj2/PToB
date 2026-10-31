#!/usr/bin/env python3
"""Selected production SS3 model/renderer/ownership tests; no device access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);out=a.out.resolve()
 sys.path.insert(0,str(REPO/'registry'));from registry import load,header
 (out/'ptob_registry.h').write_text(header(load(REPO/'registry/example.json')))
 compiler=os.environ.get('CXX','clang++');records=[]
 flags=['-DR2_TZ_WIRE=1','-DPTOB_SS3_SERVO=1','-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-I'+str(APP/'main'),'-I'+str(APP/'main/r2/r2-g1'),'-I'+str(APP/'legacy'),'-I'+str(out)]
 for name in ['servo_test','ss3_td_model_test','td_health_display_test','td_signal_test','td_publication_test','td_role_update_test','ss3_splash_test','ss3_display_clock','lifecycle_test','update_gate_test','fault_watchdog_test','ss3_lcd_io_test','ss3_durability_model_test']:
  files=[APP/'tests'/(name+'.cpp')]
  if name in ('ss3_td_model_test','td_health_display_test','ss3_splash_test'):files.append(APP/'legacy/m5stick_s3/ui.cpp')
  binary=out/name
  with (out/(name+'.log')).open('w') as log:
   extra=['-I'+str(APP/'tests/lcd_fakes')] if name=='ss3_lcd_io_test' else []
   subprocess.run([compiler,*flags,*extra,*map(str,files),'-o',str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True)
   subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  records.append(name)
 (out/'receipt.json').write_text(json.dumps(dict(schema=1,status='PASSED',tests=records,hardware_access=False,precision='UNQUALIFIED'),indent=2)+'\n')
 print('PASS',len(records),'SS3 production model/renderer/owner test programs')
if __name__=='__main__':main()
