#!/usr/bin/env python3
"""Bounded exact-device LoRa UTC-job operator. Default is a read-only query.

An explicit --request submits one boot-bound META+precision job. It does not
flash, reset, change profiles/RF, reconnect or bypass physical/source guards.
Private configuration and output belong under ignored artifacts/. MIT.
"""
import argparse
import datetime
import fcntl
import json
import os
from pathlib import Path
import re
import select
import subprocess
import termios
import time
import tty
from console_capture import Frames, close_serial, resolve, validate_config, request_cursor


def operate(query, *, request=False, token=None, delay_ms=0, allow_over_cap=False):
    """Single mutation, even if its response is lost. Polling never resubmits."""
    current=query('lora-utc')
    if not current.get('supported') or current.get('result'):
        raise ValueError('UTC-job API unsupported or query refused')
    if not request:return current
    floor=int(current['floor'],16)
    chosen=floor+1 if token is None else token
    if not 0<chosen<2**64 or chosen<floor:
        raise ValueError('valid monotonic token required; reboot token floor exhausted')
    if not 0<=delay_ms<=10000:raise ValueError('delay outside 0..10000ms')
    options=delay_ms|(0x80000000 if allow_over_cap else 0)
    current=query(f'lora-utc {chosen:016x} {options:08x}')
    if current.get('result'):raise ValueError('UTC request refused: '+json.dumps(current,sort_keys=True))
    if current.get('token')!=f'{chosen:016x}':raise ValueError('UTC response token mismatch')
    while current['state'] in (1,2):
        time.sleep(.25)
        current=query('lora-utc')
        if current.get('token')!=f'{chosen:016x}':raise ValueError('UTC job replaced unexpectedly')
    return current


def private_destination(out):
    out=out.resolve()
    root=next((p for p in out.parents if (p/'.git').exists()),None)
    if root is None or out.relative_to(root).parts[0]!='artifacts':
        raise ValueError('raw output must be under an ignored repository artifacts directory')
    r=subprocess.run(['git','-C',str(root),'check-ignore','--quiet','--no-index','--',str(out.relative_to(root))],capture_output=True,timeout=5)
    if r.returncode!=0:raise ValueError('raw output path is not ignored')
    return out


def require_closed(state):
    if not state.get('closed'):raise OSError('operator teardown failed; see closure evidence')


class Console:
    """One finite exclusive descriptor; no retry or reconnect path."""
    def __init__(self, unit, config, out, seconds, expected_boot=None):
        self.unit,self.config,self.out=unit,config,out
        self.deadline=time.monotonic()+seconds
        self.boot=expected_boot or 0;self.expected_boot=expected_boot
        self.fd=self.lock=None;self.port=None;self.checked=0
        self.buffer=b'';self.frames=Frames();self.latest=None
        self.request=int(time.time()*1000)&0xffffffff
        self.raw=(out/'serial.raw').open('xb');self.journal=(out/'commands.jsonl').open('x')
        self.state=dict(unit=unit,mutation_count=0,closed=False)

    def record(self, kind, **value):
        self.journal.write(json.dumps(dict(kind=kind,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),host_mono_ns=time.monotonic_ns(),**value))+'\n');self.journal.flush()

    def __enter__(self):
        try:
            self.lock=open('/tmp/ptob-lab-'+self.unit+'.lock','a');fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            self.port=resolve(self.config['usb_serial'])
            self.fd=os.open(self.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            fcntl.ioctl(self.fd,termios.TIOCEXCL);tty.setraw(self.fd,termios.TCSANOW)
            attrs=termios.tcgetattr(self.fd);attrs[4]=attrs[5]=termios.B115200;termios.tcsetattr(self.fd,termios.TCSANOW,attrs)
            self.query('status');return self
        except BaseException:
            self.__exit__(None,None,None);raise

    def pump(self):
        if not select.select([self.fd],[],[],.02)[0]:return
        data=os.read(self.fd,8192)
        if not data:raise OSError('USB disconnected; no reconnect')
        if self.raw.tell()+len(data)>2*1024**2:raise OSError('finite raw-log limit reached')
        self.raw.write(data);self.raw.flush();self.buffer+=data
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            reply=self.frames.feed(line.decode('ascii'))
            if reply:self.latest=reply
        if len(self.buffer)>8192:raise ValueError('unterminated console record')

    def query(self, op):
        if op not in ('status','lora-utc') and not re.fullmatch(r'lora-utc [0-9a-f]{16} [0-9a-f]{8}',op):
            raise ValueError('operator command outside exact UTC-job scope')
        if time.monotonic()>=self.deadline:raise TimeoutError('finite operator deadline')
        if time.monotonic()-self.checked>=5:
            if resolve(self.config['usb_serial'],os.getpid())!=self.port:raise OSError('USB mapping changed')
            self.checked=time.monotonic()
        if time.monotonic()>=self.deadline:raise TimeoutError('operator deadline during ownership check')
        self.request+=1
        if self.request>0xffffffff:raise OverflowError('request sequence exhausted')
        mutating=op.startswith('lora-utc ')
        if mutating:
            if self.expected_boot is None:raise ValueError('mutation requires exact expected boot')
            if self.state['mutation_count']:raise ValueError('single mutation already spent')
            self.state['mutation_count']+=1
        command=f'r2 {self.request:08x} {self.config["silicon_id"]} {self.config["source"]} {self.boot:016x} {op}\n'.encode()
        self.latest=None;self.record('query',op=op,command=command.decode().strip())
        if os.write(self.fd,command)!=len(command):raise OSError('partial write; request never retried')
        until=min(self.deadline,time.monotonic()+2.5)
        while time.monotonic()<until:
            self.pump()
            if self.latest is None or self.latest[0]!=self.request:continue
            value=self.latest[1];observed=int(value.get('boot','0'),16)
            if not observed or (self.boot and self.boot!=observed):raise ValueError('boot changed; no reconnect')
            if value.get('source')!=self.config['source']:raise ValueError('firmware source mismatch')
            if op=='status' and any(value.get(k)!=v for k,v in dict(unit=self.unit,silicon=self.config['silicon_id'],commit=self.config['commit']).items()):raise ValueError('exact device/build mismatch')
            if op=='status':self.request=request_cursor(value,self.request)
            self.boot=observed;self.state['boot']=f'{observed:016x}';self.record('reply',op=op,payload=value);return value
        raise TimeoutError('missing bound response; request never retried')

    def __exit__(self, *_):
        released,errors=[],[]
        if self.fd is not None:released,errors=close_serial(self.fd);self.fd=None
        for stream in (self.lock,self.raw,self.journal):
            if stream is not None:
                try:stream.close()
                except OSError as e:errors.append(repr(e))
        self.state.update(closed=not errors and not released,close_errors=errors,exclusive_release_errors=released,partial_tail=bool(self.buffer or self.frames.header))
        (self.out/'closure.json').write_text(json.dumps(self.state,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True);p.add_argument('--unit',required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--request',action='store_true');p.add_argument('--expected-boot',help='exact 16-digit current boot; required to mutate')
    p.add_argument('--token',type=lambda x:int(x,16));p.add_argument('--delay-ms',type=int,default=0);p.add_argument('--allow-over-cap',action='store_true')
    a=p.parse_args();devices=json.loads(a.config.read_text())['devices'];validate_config(devices)
    if a.unit not in devices:p.error('exact unit missing from configuration')
    if (a.expected_boot is not None and not re.fullmatch(r'[0-9a-f]{16}',a.expected_boot)) or a.expected_boot=='0'*16:p.error('invalid boot')
    if a.request and a.expected_boot is None:p.error('--request requires --expected-boot')
    if not a.request and (a.token is not None or a.delay_ms or a.allow_over_cap):p.error('job options require --request')
    if not 0<=a.delay_ms<=10000 or (a.token is not None and not 0<a.token<2**64):p.error('invalid job options')
    os.umask(0o077)
    out=private_destination(a.out);out.mkdir(mode=0o700,parents=True,exist_ok=False)
    result=dict(status='INCOMPLETE',hardware=True,request=a.request,allow_over_cap=a.allow_over_cap)
    try:
        with Console(a.unit,devices[a.unit],out,40,int(a.expected_boot,16) if a.expected_boot else None) as c:
            result['job']=operate(c.query,request=a.request,token=a.token,delay_ms=a.delay_ms,allow_over_cap=a.allow_over_cap)
        result['job_outcome']='COMPLETED' if not a.request or result['job']['state']==3 else 'JOB_EXPIRED'
        result['closure']=c.state;require_closed(c.state)
        result['status']=result['job_outcome']
    except BaseException as e:
        result.update(error=repr(e),ownership=getattr(e,'evidence',None));raise
    finally:
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['status']=='COMPLETED' else 2
if __name__=='__main__':raise SystemExit(main())
