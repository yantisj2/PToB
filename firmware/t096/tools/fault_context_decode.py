#!/usr/bin/env python3
"""Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
Offline WD-context decoder. No CPU registers/frames are invented or unwound.
"""
import argparse,json,struct,zlib
from pathlib import Path
from crash_decode import load
from rc_check_sites import resolve as resolve_rc_check
def rc_check_detail(reason,word):
    error=(word>>16)&65535;error=error-65536 if error&32768 else error
    detail=word&65535;site=(detail>>3)&4095
    if reason!=5 or error!=-5 or not detail&32768 or not detail&2 or not site:return None
    return dict(encoding='RC_CHECK_LINE12_EIO_V1',site=site,service_error=-5,
      resolved=False,status='ORIGINAL_SOURCE_MAP_REQUIRED',file='UNKNOWN',line=None,why='UNKNOWN',
      API_return='UNKNOWN',meaning='First failed boolean check; genericEIO is not original HCI/API return')


def decode(raw):
    assert len(raw)==512
    p=struct.unpack('<128I',raw);w=p[64:98]
    assert p[0]==0x57444631 and p[1]==1 and p[2]<16 and p[3]==512
    assert p[127]==0x434f4d31 and zlib.crc32(raw[:504])==p[126]
    assert p[4]or p[5];assert p[6]or p[7];assert p[56]or p[57]
    assert not any(p[8:16]) and p[26]in(1,2)
    observed=p[26]==2
    assert w[0]==(0x52325253 if observed else 0x52325744) and w[1]==(2 if observed else 1)
    assert zlib.crc32(raw[260:388])==w[33]
    assert p[4:6]==w[2:4] and p[24]==w[4]
    binding=raw[304:368].decode('ascii')
    factory=raw[368:384].decode('ascii')
    assert len(binding)==64 and all(c in '0123456789abcdef'for c in binding)
    assert len(factory)==16 and all(c in '0123456789abcdef'for c in factory)
    assert raw[64:96].hex()==binding and factory==f'{p[7]:08x}{p[6]:08x}'
    reason=p[24];terminal=not observed and(2<=reason<=6 or 16<=reason<256)
    assert(not reason and p[27]==w[5]and p[27])if observed else not p[27]
    assert p[25]==int(terminal)
    assert not any(p[i]for i in range(28,64)if i not in(56,57,58))
    assert not any(p[98:126])
    names={2:'RETRIES_EXHAUSTED',3:'CORE_STALL',4:'RECOVERY_FAILED',5:'CONTROL_STALL',6:'LORA_FAULT'}
    cause=names.get(reason,'FATAL_HOOK'if 16<=reason<256 else'UNKNOWN')if terminal else'UNKNOWN'
    return dict(kind='BARE_HARDWARE_RESET'if observed else'WATCHDOG_CONTEXT',schema=1,cpu_exception_frame=False,
        originating_source='UNKNOWN',exact_build_binding=binding,silicon=factory,
        origin_boot='UNKNOWN'if observed else f'{p[5]:08x}{p[4]:08x}',
        observed_reset_boot=f'{p[5]:08x}{p[4]:08x}'if observed else'UNKNOWN',
        hardware_reset_flags=p[27]if observed else None,binding_role='OBSERVING_IMAGE'if observed else'ORIGIN_IMAGE',
        software_terminal_decision=terminal,
        cause=cause,raw_reason=reason,raw_capsule=list(w),slot=p[2],
        rc_check=rc_check_detail(reason,w[32])if terminal else None,
        generation=p[56]|p[57]<<32,retained_id=f'{p[58]:08x}',
        limits='Original source absent in136B capsule; match original build receipt by binding. Stored context is not current-reset attribution and contains no CPU exception frame.')

def main():
    p=argparse.ArgumentParser();p.add_argument('input',type=Path);p.add_argument('--first-chunk',type=lambda s:int(s,0),default=512)
    p.add_argument('--build',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    raw,current=load(a.input,a.first_chunk);d=decode(raw);d['readout_identity']=current
    if a.build:
        r=json.loads((a.build/'receipt.json').read_text());device=json.loads((a.build/'device.json').read_text())
        assert r['watchdog_binding_sha256']==d['exact_build_binding']and device['device_id']==d['silicon'],'Wrong exact original build'
        d.update({('observing_source'if d['kind']=='BARE_HARDWARE_RESET'else'originating_source'):r['source_sha256'],
                  ('observing_commit'if d['kind']=='BARE_HARDWARE_RESET'else'originating_commit'):r['child_commit']})
        if d.get('rc_check'):
            d['rc_check'].update(resolve_rc_check(a.build,d['exact_build_binding'],d['rc_check']['site']))
    a.out.write_text(json.dumps(d,indent=2)+'\n')
if __name__=='__main__':main()
