#!/usr/bin/env python3
"""Render actual CoreS3-SE RGB565 UI to PNG, using explicitly synthetic fixtures.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,struct,subprocess,zlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def png(path,rgb,width=320,height=240):
 def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
 rows=b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
 meta=b'Origin\0Actual CoreS3-SE C++ renderer; synthetic fixture, not hardware or accuracy evidence. Copyright 2026 Jonathan Yantis. MIT.'
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'tEXt',meta)+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 command=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(APP/'main'),str(APP/'tools/preview.cpp'),str(APP/'main/ui.cpp'),str(APP/'main/mesh_diagnostics.cpp'),'-o',str(out/'preview')]
 subprocess.run(command,check=True);subprocess.run([str(out/'preview'),str(out)],check=True)
 assets={}
 for f in sorted(out.glob('*.ppm')):
  rgb=f.read_bytes().split(b'\n',3)[3];assert len(rgb)==320*240*3
  target=f.with_suffix('.png');png(target,rgb);assets[target.name]=hashlib.sha256(target.read_bytes()).hexdigest()
 # Contact sheets retain native pixels, without resampling.
 for filename,names in [('contact-sheet',['launcher','ptob','diagnostics','launcher-search','ptob-rc9','ptob-stale']),('diagnostics-sheet',['diag-mesh','diag-path','diag-quality','diag-reference','diag-wire','diag-shadow']),('shadow-review',['ptob','diag-shadow','diag-quality','ptob-utc','diag-shadow-warm','diag-shadow-units']),('reference-states',['diag-reference','diag-reference-expired','diag-reference-unknown','diag-reference-cached-no','diag-reference-last-gm','diag-reference-long']),('hot-states',['diag-mesh','diag-hot-cached','diag-nominee','diag-no-hot','diag-long-names','diag-yield']),('gm7-states',['diag-wire','diag-wire-no-utc','diag-wire-other-scope','diag-wire-legacy','diag-nominee','diag-yield']),('diagnostics-states',['diag-no-hot','diag-no-packets','diag-rc9-quality','diag-direct-gnss','diag-stale','diag-long-names'])]:
  sheet=bytearray(960*480*3)
  for i,name in enumerate(names):
   rgb=(out/(name+'.ppm')).read_bytes().split(b'\n',3)[3]
   for y in range(240):
    start=((i//3*240+y)*960+i%3*320)*3;sheet[start:start+960]=rgb[y*960:(y+1)*960]
  png(out/(filename+'.png'),bytes(sheet),960,480)
 sources=[APP/'main/watch_faces.inc',APP/'main/ui.cpp',APP/'main/ui.hpp',APP/'main/holdover_view.hpp',APP.parent/'w12/main/coarse_time_zone.hpp',APP.parent/'w12/main/r2/r2-g1/time_zone.h',APP/'main/mesh_diagnostics.hpp',APP/'main/mesh_diagnostics.cpp',APP/'tools/preview.cpp',APP/'tools/preview.py',APP.parent/'n30/main/font.inc']
 receipt={'schema':1,'scope':'ACTUAL_FIRMWARE_RENDERER_SYNTHETIC_FIXTURES','dimensions':[320,240],'hardware_access':False,'precision_evidence':False,'copyright':'2026 Jonathan Yantis','license':'MIT','sources':{str(f.relative_to(APP.parents[1])):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},'assets':assets}
 (out/'preview.json').write_text(json.dumps(receipt,indent=2)+'\n');print(out/'contact-sheet.png')
if __name__=='__main__':main()
