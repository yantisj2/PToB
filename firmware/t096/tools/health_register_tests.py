#!/usr/bin/env python3
"""Actual fatal register producer/strict decoder at fake ARM boundaries; offline.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
import argparse,hashlib,json,os,struct,subprocess,zlib
from pathlib import Path
from controller_fault_decode import decode_register_capsule,decode_prepare_capsule
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
a=p.parse_args();app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
inputs=[app/'src/health.c',app/'tests/health_register_test.c',app/'tests/health_fake.h',Path(__file__).resolve(),app/'tools/controller_fault_decode.py']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
bound={str(f):sha(f)for f in inputs};passed=False
try:
 for name in ['zephyr/fatal.h','zephyr/irq.h','zephyr/kernel.h','zephyr/timing/timing.h','cmsis_core.h','build_identity.h']:
  f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* ARM boundary supplied by host fixture. */\n')
 cc=os.environ.get('CC','clang')
 command=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(app/'tests/health_register_test.c'),'-o',str(out/'register-test')]
 with (out/'run.log').open('x')as log:
  subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'register-test'),str(out/'registers.bin')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
 words=list(struct.unpack('<64I',(out/'registers.bin').read_bytes()));source='0123456789abcdef'*4;boot=0x1122334455667788
 decoded=decode_register_capsule(words,source,boot)
 assert decoded['registers']['values']['r5']==0x55555555 and decoded['registers']['values']['pc']==0x64640
 assert decoded['registers']['callee_provenance_checked'] and decoded['registers']['scb_provenance']=='POST_KERNEL_CLEAR_BFAR_VALIDITY_UNAVAILABLE'
 assert decode_prepare_capsule(words,source,boot)['prepare']['availability']=='UNAVAILABLE_SCHEMA4_REGISTERS'
 def recalc(w):w[63]=zlib.crc32(struct.pack('<62I',*w[1:63]));return w
 for schema in (2,3):
  w=words.copy();w[1]=schema;recalc(w)
  assert decode_register_capsule(w,source,boot)['registers']['availability']=='UNAVAILABLE_LEGACY'
 rejected=0
 for slot,value in [(40,32),(40,3),(40,18),(56,0xffffffe1),(62,1),(4,3),(14,3),(40,31)]:
  w=words.copy();w[slot]=value;recalc(w)
  try:decode_register_capsule(w,source,boot)
  except ValueError:rejected+=1
  else:raise AssertionError((slot,value))
 for source_arg,boot_arg in [(source,boot+1),('f'*64,boot)]:
  try:decode_register_capsule(words,source_arg,boot_arg)
  except ValueError:rejected+=1
  else:raise AssertionError('unbound failed boot/source accepted')
 w=words.copy();w[47]^=1
 try:decode_register_capsule(w,source,boot)
 except ValueError:rejected+=1
 else:raise AssertionError('bad CRC accepted')
 # Native assembly/header are immutable inputs, not simulated RF/CPU proof.
 zephyr=a.deps.resolve()/'zephyr';assembly=zephyr/'arch/arm/core/cortex_m/fault_s.S';header=zephyr/'include/zephyr/arch/arm/thread.h';fault=zephyr/'arch/arm/core/cortex_m/fault.c'
 text=assembly.read_text();assert 'push {r0, lr}'in text and 'push { r1, r2 }'in text and 'push {r4-r11}'in text and 'mov  r3, sp'in text and 'add sp, #40'in text
 h=header.read_text();names=['v1','v2','v3','v4','v5','v6','v7','v8','psp'];assert [h.index('uint32_t '+n+';')for n in names]==sorted(h.index('uint32_t '+n+';')for n in names)
 assert '.callee = callee_regs, .exc_return = exc_return, .msp = msp'in fault.read_text()
 (out/'decoded.json').write_text(json.dumps(decoded,indent=2)+'\n')
 inputs.extend([assembly,header,fault]);bound.update({str(f):sha(f)for f in inputs})
 assert all(sha(Path(f))==v for f,v in bound.items());passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'class':'ACTUAL_FATAL_PRODUCER_FAKE_ARM_BOUNDARIES','inputs':bound,'command':locals().get('command'),'decoder_rejections':locals().get('rejected',0),'limits':['Synthetic exception frames/locks/stack address range; not CPU exception or retained RAM survival qualification','SCB values are post-kernel-clear; no recovered BFAR validity','Register provenance is descriptive and never role/time authority','Source175 diagnostic candidate does not fix the unexplained174 fault']},indent=2)+'\n')
print('PASS actual format4 producer/decoder/negative provenance/legacy2+3 and pinned entry layout; hardware UNKNOWN')
