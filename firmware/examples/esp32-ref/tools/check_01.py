#!/usr/bin/env python3
"""Compile the wireless controller, simulate BLE, and exercise UI/power offline.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shlex,struct,subprocess,sys,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def png(path,frames):
    # Real firmware framebuffers at 4x nearest-neighbor. Panel color bands
    # are illustrative; geometry/text are exactly the compiled C renderer.
    columns=min(2,len(frames))
    width=columns*512+(columns-1)*16
    height=((len(frames)+columns-1)//columns)*272-16
    rows=[]
    for y in range(height):
        row=bytearray();py=(y%272)//4
        for x in range(width):
            column=x//528;index=(y//272)*columns+column;px=(x%528)//4
            on=index<len(frames) and px<128 and py<64 and frames[index][(py//8)*128+px]&(1<<(py%8))
            row.extend((135,235,125) if on and py<16 else (108,185,255) if on else (5,8,11))
        rows.append(b'\0'+row)
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--sanitize',action='store_true',help='fatal UndefinedBehaviorSanitizer; ASan remains host-limited')
    p.add_argument('--clock', choices=['gptimer20','mcpwm80'], default='gptimer20')
    a=p.parse_args();out=a.out.resolve()
    if out.exists() or out==ROOT or ROOT in out.parents:p.error('fresh output outside the source required')
    os.umask(0o077);out.mkdir(parents=True)
    sources={str(q.relative_to(ROOT)):digest(q) for q in ROOT.rglob('*') if q.is_file() and '__pycache__' not in q.parts}
    counter_hz=80000000 if a.clock=='mcpwm80' else 20000000
    receipt={'schema':1,'status':'FAILED','hardware_access':False,'evidence_kind':'DETERMINISTIC_HOST_EXECUTION','sources':sources,'commands':[],'counter_hz':counter_hz}
    cc=shlex.split(os.environ.get('CC','cc'))
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror']
    flags.append('-DREF_CLOCK_HZ='+str(counter_hz))
    if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
    folders=['components/ref_clock','components/ref_softpll','components/ref_protocol','components/ref_ui',
             '01_wireless_softpll/ports/esp32s3','01_wireless_softpll/ports/w12','tests']
    includes=[v for folder in folders for v in ['-I',str(ROOT/folder)]]
    common=['components/ref_clock/ref_clock.c','components/ref_softpll/ref_softpll.c','components/ref_protocol/ref_wire.c',
            'components/ref_protocol/ref_join.c','components/ref_protocol/ref_session.c','components/ref_ui/ref_button.c',
            'components/ref_ui/ref_display.c','components/ref_ui/ref_shutdown.c','01_wireless_softpll/ports/esp32s3/radio_math.c']
    def run(command,name):
        receipt['commands'].append([str(v) for v in command])
        result=subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
        (out/name).write_text(result.stdout)
        if result.returncode:raise RuntimeError('failed: '+str(out/name))
        return result.stdout
    try:
        objects=[]
        for name in common:
            obj=out/(Path(name).stem+'.o');objects.append(str(obj))
            run(cc+flags+includes+['-MMD','-MF',str(obj)+'.d','-c',str(ROOT/name),'-o',str(obj)],obj.stem+'-compile.log')
        for name,source in [('wireless_test','tests/wireless_test.c'),('wireless_simulation','01_wireless_softpll/simulation.c'),('display_preview','tests/display_preview.c')]:
            run(cc+flags+includes+objects+[str(ROOT/source),'-lm','-o',str(out/name)],name+'-link.log')
            print(run([str(out/name)]+([str(out)] if name=='display_preview' else []),name+'.log'),end='')
        run(cc+flags+includes+['-DREF_POWER_TEST',str(ROOT/'01_wireless_softpll/ports/w12/power.c'),str(ROOT/'tests/power_test.c'),'-o',str(out/'power_test')],'power-compile.log')
        print(run([str(out/'power_test')],'power.log'),end='')
        for dep in out.glob('*.d'):
            for name in shlex.split(dep.read_text().replace('\\\n',' '))[1:]:
                if ROOT not in Path(name).resolve().parents:raise RuntimeError('non-system dependency outside collection: '+name)
        frames=sorted(out.glob('display-*.bin'))
        if not frames:raise RuntimeError('no W12 preview frames were produced')
        png(out/'w12-preview.png',[frame.read_bytes() for frame in frames])
        for frame in frames:
            name=frame.stem.split('-',2)[2]
            png(out/('w12-'+name+'.png'),[frame.read_bytes()])
        if any(digest(ROOT/name)!=value for name,value in sources.items()):raise RuntimeError('source changed during checks')
        receipt['status']='HOST_CHECKS_PASSED'
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        receipt['error']=str(error);print(error,file=sys.stderr)
    finally:
        receipt['artifacts']={q.name:digest(q) for q in out.iterdir() if q.is_file()}
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Receipt:',out/'receipt.json')
    return 0 if receipt['status']=='HOST_CHECKS_PASSED' else 1
if __name__=='__main__':sys.exit(main())
