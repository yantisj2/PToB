#!/usr/bin/env python3
"""Offline actual raw-capture owner and sole-counter guards. No hardware calls.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
 src=(APP/'main/native_main.cpp').read_text();(out/'actual_fine_update_gate.hpp').write_text(src[src.index('[[gnu::noinline]] void poll_update_drain('):src.index('[[gnu::noinline]] void poll_display_action(')])
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 for path in (out,APP/'tests/fine_fakes',APP/'main',APP.parent/'w12/main'):flags+=['-I'+str(path)]
 for name in ('fine_counter','fine_capture','fine_capture_native'):
  with (out/(name+'-compile.log')).open('w') as log:subprocess.run([cxx,*flags,APP/'tests'/(name+'_test.cpp'),'-o',out/name],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
  with (out/(name+'.log')).open('w') as log:subprocess.run([out/name],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
 spec=importlib.util.spec_from_file_location('n30_fine_console',APP/'tools/framed_console.py');console=importlib.util.module_from_spec(spec);spec.loader.exec_module(console)
 for filename in ('fine-frame.txt','fine-native-frame.txt'):
  rows={line.split()[0]:console.fields(line) for line in (out/filename).read_text().splitlines()};console.validate_fine(rows)
 rows={line.split()[0]:console.fields(line) for line in (out/'fine-frame.txt').read_text().splitlines()}
 for row,key,value in [('N30_FINE_STATUS','hz','20000000'),('N30_FINE_STATUS','count','5'),('N30_FINE_STATUS','latest80','-1'),('N30_FINE_EDGE0','incarnation','999'),('N30_FINE_CONTRACT','clock_update','YES'),('N30_FINE_CONTRACT','coalesced','0')]:
  altered={name:dict(values) for name,values in rows.items()};altered[row][key]=value
  try:console.validate_fine(altered)
  except Exception:pass
  else:raise AssertionError('fine parser accepted '+row+'.'+key)
 synthetic={line.split()[0]:console.fields(line) for line in (out/'fine-native-frame.txt').read_text().splitlines()}
 for row,key,value in [('N30_FINE_STATUS','probe_goal','15'),('N30_FINE_STATUS','probe_spacing_ms','49'),('N30_FINE_EDGE0','provenance','NATIVE_SIGNAL'),('N30_FINE_STATUS','issued0','7'),('N30_FINE_CONTRACT','RX','SYNCFOUND_230')]:
  altered={name:dict(values) for name,values in synthetic.items()};altered[row][key]=value
  try:console.validate_fine(altered)
  except Exception:pass
  else:raise AssertionError('synthetic parser accepted '+row+'.'+key)
 paths=[*sorted((APP/'tests/fine_fakes').rglob('*')),APP/'tools/framed_console.py',APP/'main/native_main.cpp',APP/'main/fine_counter.hpp',APP/'main/fine_capture.cpp',APP/'main/fine_capture.hpp',APP/'tests/fine_counter_test.cpp',APP/'tests/fine_capture_test.cpp',APP/'tests/fine_capture_native_test.cpp']
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_OWNER_SYNTHETIC_PERIPHERAL_AND_COUNTER_ASAN_UBSAN','hardware_access':False,'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}},indent=2)+'\n')
 print('PASS fine raw-owner and counter lifecycle with ASan/UBSan; physical endpoint remains unknown')
if __name__=='__main__':main()
