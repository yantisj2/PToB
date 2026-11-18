#!/usr/bin/env python3
"""Offline native user-space register parser/burst checks on ordinary files.

Copyright (c) 2026 Jonathan Yantis. MIT. No USB, SSH, debugfs or clocks changed.
"""
import argparse
import ctypes
import errno
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[3]


class Read(ctypes.Structure):
    _fields_=[('before',ctypes.c_uint64),('after',ctypes.c_uint64),('mfindex',ctypes.c_uint32),('valid',ctypes.c_uint32)]


class Pair(ctypes.Structure):
    _fields_=[('before',ctypes.c_uint64),('realtime',ctypes.c_uint64),('after',ctypes.c_uint64)]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if not out.is_relative_to(ROOT/'artifacts'):raise ValueError('output must be ignored artifacts/')
    out.parent.mkdir(parents=True,exist_ok=True);out.mkdir(mode=0o700)
    source=Path(__file__).with_name('runtime_reader.c');cc=shutil.which('cc')
    if not cc:raise RuntimeError('public C compiler required')
    cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-fPIC','-shared',str(source),'-o',str(out/'reader.so')]
    b=subprocess.run(cmd,capture_output=True,text=True,timeout=20);(out/'build.log').write_text(b.stdout+b.stderr)
    if b.returncode:raise RuntimeError('native compile failed; see build.log')
    lib=ctypes.CDLL(str(out/'reader.so'),use_errno=True);f=lib.ptob_runtime_burst
    f.argtypes=[ctypes.c_int,ctypes.c_uint32,ctypes.c_uint64,ctypes.POINTER(Read),ctypes.POINTER(ctypes.c_uint32)];f.restype=ctypes.c_int
    rows=(Read*24)();count=ctypes.c_uint32();cases=[]
    fixture=out/'ordinary-file';fixture.write_bytes(b'MFINDEX      = 0x00003fff\nIR0_IMAN = 0x00000000\n')
    with fixture.open('rb') as stream:
        assert f(stream.fileno(),24,500000,rows,ctypes.byref(count))==0 and count.value==24
        assert all(r.valid and r.mfindex==16383 and r.after>=r.before for r in rows);cases.append('bounded24read_constant_file')
        fixture.write_bytes(b'MFINDEX      = 0x00000000\n')
        assert f(stream.fileno(),1,500000,rows,ctypes.byref(count))==0 and count.value==1 and rows[0].mfindex==0;cases.append('fresh_positional_read')
        for text in (b'MFINDEX = 0x00004000\n',b'MFINDEX = 0x0000000g\n',b'XFINDEX = 0x00000000\n',b'MFINDEX = 0x00000000',b''):
            fixture.write_bytes(text);ctypes.set_errno(0)
            assert f(stream.fileno(),1,500000,rows,ctypes.byref(count))==-1 and ctypes.get_errno()==errno.EPROTO
            assert count.value==1 and not rows[0].valid;cases.append('malformed_rejected_with_partial_bracket')
        for limit,duration in ((0,500000),(25,500000),(1,0),(1,500001)):
            assert f(stream.fileno(),limit,duration,rows,ctypes.byref(count))==-1 and count.value==0;cases.append('budget_rejected_before_read')
    pair=lib.ptob_realtime_pair;pair.argtypes=[ctypes.POINTER(Pair)];pair.restype=ctypes.c_int
    ctypes.set_errno(0);assert pair(None)==-1 and ctypes.get_errno()==errno.EINVAL;cases.append('clock_pair_null_refused')
    widths=[]
    for _ in range(100):
        before=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW);rt_before=time.time_ns();row=Pair()
        assert pair(ctypes.byref(row))==0
        rt_after=time.time_ns();after=time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        assert before<=row.before<=row.after<=after and rt_before<=row.realtime<=rt_after
        assert row.realtime>2**53 and type(row.realtime) is int
        widths.append(row.after-row.before)
    cases.extend(['clock_pair100complete_outer_containments','clock_pair_integer_epoch_above2pow53'])
    # Deterministic replacement is compiled into a separate ordinary-file test
    # library. It never loads in live capture or replaces an OS clock.
    prefix='''#define _GNU_SOURCE
#include <errno.h>
#include <time.h>
static int scenario,step;
static int ptob_test_clock_gettime(clockid_t clock,struct timespec *t){
 const clockid_t order[3]={CLOCK_MONOTONIC_RAW,CLOCK_REALTIME,CLOCK_MONOTONIC_RAW};
 if(step>=3||clock!=order[step]){errno=EPROTO;return -1;}
 ++step;if(scenario==step){errno=EIO;return -1;}
 t->tv_sec=step==2?1790000000:0;t->tv_nsec=step==1?100:step==2?1:200;
 if(scenario==4&&step==3)t->tv_nsec=90;
 if(scenario==5&&step==2)t->tv_sec=-1;
 return 0;
}
'''
    suffix='''
int ptob_test_pair(int value,struct ptob_realtime_pair *out){scenario=value;step=0;return ptob_realtime_pair(out);}
int ptob_test_steps(void){return step;}
'''
    fault_source=out/'clock-fixture.c';fault_source.write_text(prefix+source.read_text().replace('clock_gettime(', 'ptob_test_clock_gettime(')+suffix)
    fault_cmd=cmd[:-3]+[str(fault_source),'-o',str(out/'clock-fixture.so')]
    b=subprocess.run(fault_cmd,capture_output=True,text=True,timeout=20);(out/'clock-fixture-build.log').write_text(b.stdout+b.stderr)
    if b.returncode:raise RuntimeError('clock fixture compile failed; see clock-fixture-build.log')
    fault=ctypes.CDLL(str(out/'clock-fixture.so'),use_errno=True);fault.ptob_test_pair.argtypes=[ctypes.c_int,ctypes.POINTER(Pair)]
    fault.ptob_test_pair.restype=ctypes.c_int;fault.ptob_test_steps.restype=ctypes.c_int
    for value,code,steps in ((0,0,3),(1,errno.EIO,1),(2,errno.EIO,2),(3,errno.EIO,3),(4,errno.EPROTO,3),(5,errno.ERANGE,2)):
        row=Pair(999,999,999);ctypes.set_errno(0);result=fault.ptob_test_pair(value,ctypes.byref(row))
        assert result==(0 if not code else -1) and ctypes.get_errno()==code and fault.ptob_test_steps()==steps
        if not code:assert (row.before,row.realtime,row.after)==(100,1_790_000_000_000_000_001,200)
        cases.append('deterministic_clock_pair_order_error_rollback_'+str(value))
    receipt=dict(status='PASS',hardware_access=False,cases=cases,compiler_command=cmd,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        deterministic_clock_fixture=dict(source_sha256=hashlib.sha256(fault_source.read_bytes()).hexdigest(),compiler_command=fault_cmd),
        clock_pair_test=dict(n=len(widths),maximum_width_ns=max(widths),minimum_width_ns=min(widths),
            platform='local ordinary clock reads; not a Pi comparison or physical timestamp qualification'))
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))


if __name__=='__main__':main()
