#!/usr/bin/env python3
"""MIT. Jonathan Yantis2026. Exact-source map and backward WD decoding gates."""
import argparse, hashlib, importlib.util, json, struct, subprocess, sys, zlib
from pathlib import Path
A=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(A/'tools'))
import rc_check_sites as sites
from fault_context_decode import decode,rc_check_detail

def record(word,reason=5,profile=0):
    p=[0]*128;p[:8]=[0x57444631,1,3,512,0x11223344,0x55667788,1,0x10000000]
    p[24:28]=[reason,1,1,0];p[56]=1;p[58]=7;p[127]=0x434f4d31
    w=[0]*34;w[:6]=[0x52325744,1,p[4],p[5],reason,profile];w[32]=word
    raw=bytearray(struct.pack('<128I',*p));binding='01'*32
    raw[64:96]=bytes.fromhex(binding)
    capsule=bytearray(struct.pack('<34I',*w));capsule[48:112]=binding.encode();capsule[112:128]=b'1000000000000001'
    struct.pack_into('<I',capsule,132,zlib.crc32(capsule[4:132]));raw[256:392]=capsule
    struct.pack_into('<I',raw,504,zlib.crc32(raw[:504]));return bytes(raw),binding

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
    for detail in range(65536):assert rc_check_detail(5,(0xff92<<16)|detail)is None
    for mask in range(8):assert rc_check_detail(5,(0xfffb<<16)|mask)is None
    word=(0xfffb<<16)|0x8000|(3831<<3)|2
    raw,binding=record(word,profile=0);d=decode(raw)
    assert d['rc_check']['site']==3831 and not d['rc_check']['resolved']and d['raw_capsule'][5]==0
    assert rc_check_detail(0,word)is None and rc_check_detail(5,(0x8000|(3831<<3)|2))is None
    assert rc_check_detail(5,(0xfffb<<16)|0x8002)is None
    # Exercise actual compiler end-line mapping rather than assuming macro
    # invocation start. These are synthetic source/CPP boundary fixtures.
    b=o/'build';app=b/'source';(app/'src').mkdir(parents=True);(app/'include').mkdir()
    (app/'src/rc_dynamic_service.c').write_text('void f(void){\n check(\n false,\n "MULTILINE");\n}\n')
    (app/'src/rc_dynamic_transport.inc').write_text('check(false,"TRANSPORT");\n')
    (app/'include/rc_check_site.h').write_text('/* public shape fixture */\n')
    compiled=sites.parse_sites('check_at((0),("MULTILINE"),0u+4);check_at((0),("TRANSPORT"),2560u+1);',app)
    assert compiled[0]['line']==4 and compiled[0]['invocation_start_line']==2
    try:sites.parse_sites('check_at(0,"TRANSPORT",2561);check_at(0,"TRANSPORT",2561);',app);assert False
    except AssertionError:pass
    inputs={f:sites.sha(app/f)for f in sites.FILES};source='ab'*32;commit='cd'*20
    r=dict(status='BUILD_COMPLETE_PHYSICAL_UNKNOWN',source_dirty=False,source_sha256=source,
           child_commit=commit,watchdog_binding_sha256=binding,sources=inputs)
    (b/'device.json').write_text(json.dumps(dict(device_id='1000000000000001')))
    mapped=dict(schema=1,kind='RC_FIRST_CHECK_SOURCE_MAP',source_sha256=source,
        child_commit=commit,watchdog_binding_sha256=binding,silicon='1000000000000001',
        input_sha256=inputs,encoding=sites.ENCODING,sites=compiled)
    def seal(value=mapped):
        (b/'rc_check_sites.json').write_text(json.dumps(value));r['rc_check_site_map_sha256']=sites.sha(b/'rc_check_sites.json');(b/'receipt.json').write_text(json.dumps(r))
    seal();assert sites.resolve(b,binding,4)['why']=='MULTILINE'
    assert not sites.resolve(b,'02'*32,4)['resolved']and not sites.resolve(b,binding,3831)['resolved']
    r['status']='INCOMPLETE';seal();assert not sites.resolve(b,binding,4)['resolved'];r['status']='BUILD_COMPLETE_PHYSICAL_UNKNOWN';seal()
    r['source_dirty']=True;seal();assert not sites.resolve(b,binding,4)['resolved'];r['source_dirty']=False;seal()
    (b/'rc_check_sites.json').write_text('{}');assert not sites.resolve(b,binding,4)['resolved'];seal()
    bad={**mapped,'silicon':'2000000000000001'};seal(bad);assert not sites.resolve(b,binding,4)['resolved'];seal()
    bad={**mapped,'source_sha256':'00'*32};seal(bad);assert not sites.resolve(b,binding,4)['resolved'];seal()
    bad={**mapped,'sites':None};seal(bad);assert not sites.resolve(b,binding,4)['resolved'];seal()
    f=app/'src/rc_dynamic_service.c';before=f.read_text();f.write_text(before+'/* changed */');assert not sites.resolve(b,binding,4)['resolved'];f.write_text(before);seal()
    word=(0xfffb<<16)|0x8000|(4<<3)|2;raw,_=record(word,profile=0)
    (o/'record.bin').write_bytes(raw)
    command=[sys.executable,str(A/'tools/fault_context_decode.py'),str(o/'record.bin'),'--build',str(b),'--out',str(o/'decoded.json')]
    result=subprocess.run(command,capture_output=True,text=True,timeout=30);(o/'cli.log').write_text(result.stdout+result.stderr);result.check_returncode()
    d=json.loads((o/'decoded.json').read_text());assert d['rc_check']['resolved']and d['rc_check']['why']=='MULTILINE'and d['rc_check']['API_return']=='UNKNOWN'
    (b/'rc_check_sites.json').unlink();assert not sites.resolve(b,binding,4)['resolved']
    (o/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware=False,
       cases=['all65536legacyGMdetails','legacyRCpredicates','profileRaceTypedEIO','noErrorNoReset','unknownSite',
        'multilineCompilerCoordinate','duplicateCoordinateRefusal','exactMapResolution','wrongBindingSourceFactorySHAInputs',
        'incompleteDirtyMapRefusal','malformedMapUnknown','actualCLIReadWithoutToolchain','absentMapUnknown'],
       source_hashes={str(p.relative_to(A)):sites.sha(p)for p in [A/'tools/rc_check_sites.py',A/'tools/fault_context_decode.py',Path(__file__)]}),indent=2)+'\n')
    print('PASS typed RC context, legacy values and exact-source compiled-map decoder gates')
if __name__=='__main__':main()
