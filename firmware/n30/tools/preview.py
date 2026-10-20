#!/usr/bin/env python3
"""Generate page/source/saver previews using the actual N30 renderer and synthetic values.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,struct,subprocess,zlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def png(path,pixels,label,page):
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
 # Native64x128 pixels. UI viewers may enlarge with nearest-neighbor scaling.
 rows=b''.join(b'\0'+pixels[y*64:y*64+64] for y in range(128))
 meta={'Origin':'Production N30 renderer; synthetic fixture; no hardware access or precision evidence.','ClockAlias':label,'Screen':page}
 data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,128,8,0,0,0,0))
 for k,v in meta.items():data+=chunk(b'tEXt',(k+'\0'+v).encode())
 path.write_bytes(data+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))
def generate(out,label='N3-1'):
 out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
 command=[os.environ.get('CXX','clang++'),'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(APP/'main'),str(APP/'tools/preview.cpp'),str(APP/'main/ui_render.cpp'),str(APP/'main/ui_model.cpp'),'-o',str(out/'preview')]
 subprocess.run(command,check=True);subprocess.run([out/'preview',out,label],check=True)
 receipt={'schema':1,'alias':label,'dimensions':[64,128],'scope':'PRODUCTION_RENDERER_SYNTHETIC_FIXTURE','hardware_access':False,'sources':{str(p.relative_to(APP)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [APP/'tools/preview.cpp',APP/'main/ui.hpp',APP/'main/ui_model.cpp',APP/'main/ui_render.cpp',APP/'main/font.inc']},'assets':{}}
 for name in ('clock','rf','saver-ble','saver-rc1','saver-rc2','clock-relay','clock-pending','clock-grace','clock-syncing','saver-grace','saver-recovered','saver-expired'):
  pixels=(out/(name+'.pgm')).read_bytes().split(b'\n',3)[3];assert len(pixels)==8192
  target=out/('n30-'+name+'-preview.png');png(target,pixels,label,name);receipt['assets'][target.name]=hashlib.sha256(target.read_bytes()).hexdigest()
 (out/'preview.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--alias',choices=['N3-1','N3-2'],default='N3-1');a=p.parse_args();generate(a.out,a.alias)
