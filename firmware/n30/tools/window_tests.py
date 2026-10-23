#!/usr/bin/env python3
"""Actual N30 packet-window adapter and CRC schemas under a fake peripheral.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,importlib.util,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_N30=1','-DPTOB_N30_FINE_PINS=1','-DPTOB_N30_FINE_RW=1','-DV12_R2_ENABLED=1']
 for x in (APP/'tests/fine_fakes',APP/'main',APP.parent/'w12/main'):flags+=['-I'+str(x)]
 cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++')
 probe=out/'layout.cpp';probe.write_text('#include "native_transport.hpp"\n#include <cstdio>\n#include <cstddef>\nint main(){\n#ifdef PTOB_N30\nstatic_assert(offsetof(v12::periodic_rx::Stamp,pdu_length)==44);\n#endif\nstd::printf("%zu/%zu\\n",sizeof(v12::periodic_rx::Stamp),sizeof(v12::Report));}\n')
 layouts=[]
 for n30 in (False,True):
  exe=out/('layout-N30' if n30 else 'layout-W12');subprocess.run([cxx,*[v for v in flags if v!='-DPTOB_N30=1'],*(['-DPTOB_N30=1'] if n30 else []),probe,'-o',exe],check=True,timeout=30);layouts.append(subprocess.check_output([exe],text=True,timeout=10))
 assert layouts[0]==layouts[1] and layouts[0].startswith('48/'),'Stamp/Report ABI growth'
 with (out/'compile.log').open('w') as log:subprocess.run([cxx,*flags,APP/'tests/window_capture_test.cpp',APP/'main/fine_capture.cpp','-o',out/'window'],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=50)
 with (out/'run.log').open('w') as log:subprocess.run([out/'window'],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
 spec=importlib.util.spec_from_file_location('window_console',APP/'tools/framed_console.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
 for name,edges in [('window-packet.txt',False),('window-edges.txt',True),('window-packet-second.txt',False)]:
  rows={s.split()[0]:c.fields(s) for s in (out/name).read_text().splitlines()};c.validate_window(rows,edges)
 rows={s.split()[0]:c.fields(s) for s in (out/'window-late.txt').read_text().splitlines()};c.validate_window(rows,late=True)
 for tag,key,value in [('N30_WINDOW_STATUS','late_words','0/0/0/0'),('N30_WINDOW_STATUS','late_count','2'),('N30_WINDOW_LATE0','window','128'),('N30_WINDOW_LATE0','sequence','0')]:
  altered={k:dict(v) for k,v in rows.items()};altered[tag][key]=value
  try:c.validate_window(altered,late=True)
  except Exception:pass
  else:raise AssertionError('Unsafe late page accepted '+key)

 rows={s.split()[0]:c.fields(s) for s in (out/'window-packet.txt').read_text().splitlines()}
 for row,key,value in [('N30_WINDOW_PACKET','ad','00'),('N30_WINDOW_PACKET','crc','00000000'),('N30_WINDOW_STAMP','pdu_length','190'),('N30_WINDOW_PAIR','epoch','999'),('N30_WINDOW_CONTRACT','clock_update','YES'),('N30_WINDOW_CONTRACT','bridge','FINE_PHASE')]:
  altered={k:dict(v) for k,v in rows.items()};altered[row][key]=value
  try:c.validate_window(altered)
  except Exception:pass
  else:raise AssertionError('Unsafe window accepted: '+row+'.'+key)
 subprocess.run([sys.executable,'-B',APP/'tests/console_test.py'],check=True,timeout=15)
 paths=[APP/'main/fine_window.hpp',APP/'main/fine_capture.cpp',APP/'main/fine_capture.hpp',APP/'main/window_diagnostics.cpp',APP/'main/window_diagnostics_api.hpp',APP/'main/window_command.hpp',APP/'tests/window_capture_test.cpp',APP/'tools/framed_console.py',APP/'tools/window_tests.py',APP/'tests/console_test.py',APP.parent/'w12/main/periodic_rx_model.hpp',APP.parent/'w12/main/periodic_rx.cpp']
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_RW_OWNER_AND_PACKET_WINDOW_ASAN_UBSAN_PARSER','hardware_access':False,'sources':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}},indent=2)+'\n');print('PASS actual bounded packet windows; no RF accuracy assertion')
if __name__=='__main__':main()
