#!/usr/bin/env python3
"""N30 actual-clock acquisition and intermittent-parent tests. Offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Prepare public codec objects with firmware/w12/tools/g1_host_tests.py first.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1];W12=REPO/'firmware/w12'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--objects',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);objects=a.objects.resolve()
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_N30=1']
 for path in (APP/'main',W12/'main',W12/'legacy',W12/'legacy/reference',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include'):flags+=['-I'+str(path)]
 codecs=[objects/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
 results={}
 for name,source,linked,args in [('recovery',APP/'tests/recovery_budget_test.cpp',[],['parent','lora']),('precision',APP/'tests/precision_acquisition_test.cpp',codecs,[None]),('shared-clock',W12/'tests/g1_corrected_clock_test.cpp',codecs,[None])]:
  exe=out/name
  with (out/(name+'-compile.log')).open('w') as f:subprocess.run([cxx,*flags,source,*linked,'-o',exe],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
  for arg in args:
   label=name+('-'+arg if arg else '')
   with (out/(label+'.log')).open('w') as f:r=subprocess.run([str(exe),*([arg] if arg else [])],stdout=f,stderr=subprocess.STDOUT,timeout=30)
   results[label]=r.returncode
 paths=[APP/'main/lora_policy.hpp',APP/'tests/recovery_budget_test.cpp',APP/'tests/precision_acquisition_test.cpp',W12/'main/g1_recovery_budget.hpp',W12/'main/g1_parent_selection.hpp',W12/'main/g1_corrected_clock.hpp',W12/'main/g1_clock_prediction.hpp',W12/'main/g1_relative_quality.hpp']
 report={'status':'PASS' if not any(results.values()) else 'FAILED','cases':results,'sources':{str(x.relative_to(REPO)):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths},'scope':'SYNTHETIC_CAPTURE_CLOCK_AND_OSCILLATOR;NO_RF_UTC_OR_OUTPUT_QUALIFICATION','hardware_access':False}
 (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
 if report['status']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
