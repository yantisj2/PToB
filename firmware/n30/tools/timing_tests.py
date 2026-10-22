#!/usr/bin/env python3
"""N30 runtime timing diagnostics; portable fake OS, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 for path in (APP/'main',W12/'main',W12/'tests/console_fakes',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include'):flags+=['-I'+str(path)]
 exe=out/'timing'
 with (out/'compile.log').open('w') as f:subprocess.run([cxx,*flags,APP/'tests/timing_diagnostics_test.cpp','-o',exe],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
 with (out/'runtime.log').open('w') as f:subprocess.run([exe],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=15)
 cc=os.environ.get('CC',str(Path(cxx).with_name('clang')) if Path(cxx).is_absolute() else 'clang')
 obj=out/'join.o';common=[x for x in flags if x!='-std=c++20']
 with (out/'core-compile.log').open('w') as f:
  subprocess.run([cc,'-std=c11',*common,'-ffunction-sections','-fdata-sections','-c',W12/'main/r2/r2-g1/group_join.c','-o',obj],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
  subprocess.run([cxx,*flags,'-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',APP/'tests/timing_core_test.cpp',obj,'-o',out/'core'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
 with (out/'core.log').open('w') as f:subprocess.run([out/'core'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=20)
 subprocess.run([sys.executable,'-B',APP/'tests/command_test.py','--out',out/'commands'],check=True,timeout=40)
 subprocess.run([sys.executable,'-B',APP/'tests/console_test.py'],check=True,timeout=15)
 subprocess.run([sys.executable,'-B',APP/'tests/timing_analyze_test.py'],check=True,timeout=15)
 paths=[APP/'main/timing_diagnostics.hpp',APP/'main/timing_diagnostics.cpp',APP/'main/native_main.cpp',APP/'tools/framed_console.py',W12/'main/n30_timing_probe.hpp',W12/'main/periodic_rx.cpp',W12/'main/native_g1.cpp',W12/'main/g1_corrected_clock.hpp']
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_RECORDER_ADAPTER_DISPATCH_CRC_PARSER_ASAN_UBSAN_SYNTHETIC_OS','hardware_access':False,'source_sha256':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}},indent=2)+'\n')
 print('PASS bounded configurable timing diagnostics, command guards and CRC parsing')
if __name__=='__main__':main()
