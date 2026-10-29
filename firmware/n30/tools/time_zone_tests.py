#!/usr/bin/env python3
"""Current coarse TZ codecs and actual OLED renderers; offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];W=ROOT/'firmware/w12';N=ROOT/'firmware/n30'
def main():
 p=argparse.ArgumentParser();p.add_argument('--objects',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else 'clang');cxx=os.environ.get('CXX',cc+'++')
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1']
 includes=[W/'main',W/'main/gm7',W/'main/r2/r2-g1',W/'main/r2/rc7/protocol',W/'main/r2/rc7/include',N/'main']
 flags+=['-I'+str(x) for x in includes]
 def run(cmd,name):
  with (out/(name+'.log')).open('w') as f:subprocess.run(list(map(str,cmd)),check=True,stdout=f,stderr=subprocess.STDOUT,timeout=60)
 objects=[a.objects.resolve()/(n+'.o') for n in ('gm7_codec','group_auth','group_join','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
 run([cc,'-std=c11',*flags,W/'tests/time_zone_codec_test.c',*objects,'-o',out/'codec'],'codec-build');run([out/'codec'],'codec')
 vectors=json.loads((ROOT/'protocol/time-zone-v1/vectors.json').read_text())
 # Public frozen vectors and independently computed HMAC tags are checked by T096 runner;
 # exact output equality here proves the selected ESP codecs produce those same bytes.
 actual={line.split()[1]:line.split()[2] for line in (out/'codec.log').read_text().splitlines() if line.startswith('VECTOR ')}
 expected={x['name']:x['hex'] for x in vectors['vectors']};assert actual==expected
 run([cxx,'-std=c++20',*flags,N/'tests/time_zone_ui_test.cpp',N/'main/ui_model.cpp',N/'main/ui_render.cpp','-o',out/'n30-ui'],'n30-ui-build');run([out/'n30-ui',out],'n30-ui')
 run([cxx,'-std=c++20',*flags,'-Wno-unused-parameter','-DPTOB_W12_DUAL_UI=1','-I'+str(W/'legacy'),W/'tests/time_zone_ui_test.cpp','-o',out/'w12-ui'],'w12-ui-build');run([out/'w12-ui',out],'w12-ui')
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'checks':['exact public TZ vectors','all signed16 offsets and wire-byte mutations','coarse lease/conflict/expiry/overflow','actual W12/N30 digital analog saver and midnight','cached local pixels expire with TZ','UTC timing and fallback preserved']},indent=2)+'\n')
 print('PASS current TZ codecs and W12/N30 production OLED renderers; no hardware access')
if __name__=='__main__':main()
