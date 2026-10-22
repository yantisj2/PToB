#!/usr/bin/env python3
"""Actual pinned prepare enqueue/MFIFO, scratch writer and schema3 fatal copy.
MIT. Copyright (c) 2026 Jonathan Yantis. No hardware/LLL integration claim.
"""
import argparse,hashlib,json,os,shutil,struct,subprocess,tempfile
from pathlib import Path
from ticker_backport import generate
import controller_diag_hooks as hooks
from controller_fault_decode import decode_prepare_capsule
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);passed=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
headers={'zephyr/irq.h':'unsigned irq_lock(void);void irq_unlock(unsigned);',
 'zephyr/kernel.h':'#include <stdint.h>\nuint32_t k_uptime_get_32(void);',
 'ble_service.h':'#include <stddef.h>\n#include <stdint.h>\nint r2_console_note(const char *,size_t,uint64_t);',
 'diagnostic.h':'int r2_diag_state(void);',
 'build_identity.h':'#define R2_SOURCE_SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"',
 'zephyr/fatal.h':'','zephyr/timing/timing.h':'','cmsis_core.h':''}
for n,s in headers.items():
 f=out/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(s+'\n')
flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_CONTROLLER_FAULT_CAPTURE=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(app/'tests'),'-I'+str(a.deps.resolve()/'zephyr/subsys/bluetooth/controller/util')]
def run(label,defs,sources,args):
 with (out/(label+'.log')).open('x') as log:
  subprocess.run([*flags,*defs,*map(str,sources),'-o',str(out/(label+'.test'))],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/(label+'.test')),*map(str,args)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed.append(label)
try:
 generate(a.deps.resolve()/'zephyr',out/'backport')
 for mode in ['failure-only','verbose']:
  hooks.generate(out/'backport/ll_sw/ull.c',out/mode,mode)
  s=(out/mode/'ull.c').read_text()
  start=s.index('static __noinline struct lll_event *r2_prepare_failure(');end=s.index('\nvoid *ull_prepare_dequeue_get',start)
  (out/'prepare_enqueue.inc').write_text(s[start:end])
  if mode=='failure-only':assert 'r2_controller_diag_count(' not in s and 'r2_controller_diag_console(' not in s
  run(mode,[] if mode=='failure-only' else ['-DR2_CONTROLLER_DIAGNOSTICS=1'],[app/'tests/controller_prepare_test.c',app/'src/controller_diag.c'],[out/(mode+'.words.bin')])
 lora=(app/'src/lora_service.c').read_text();start=lora.index(' memset(&r2_diag_scratch.lora,0,',lora.index('void r2_lora_init('));end=lora.index(' binding=',start)
 (out/'lora_scratch_init.inc').write_text(lora[start:end])
 run('health',[],[app/'tests/controller_prepare_health_test.c'],[out/'failure-only.words.bin',out/'capsule.bin'])
 words=list(struct.unpack('<64I',(out/'capsule.bin').read_bytes()));source='0123456789abcdef'*4;boot=0x1122334455667788
 d=decode_prepare_capsule(words,source,boot)
 assert d['schema']==3 and d['pa']['kind']=='LATENCY_CANCEL_FAILURE_CURRENT_SLOT'
 assert d['prepare']['availability']=='COMPLETE' and d['prepare']['success_counters']=={'state':'DISABLED','values':None}
 assert d['prepare']['copied_entries']==7 and d['prepare']['entries'][3]=={'callback':0x203,'param':0x103,'is_resume':True,'is_aborted':True}
 assert d['schema3_omits']==['irq_latency','interrupted_thread_frame']
 import zlib
 def bind(w):w[63]=zlib.crc32(struct.pack('<62I',*w[1:63]));return w
 for raw in [1,7,9]:
  bad=words.copy();bad[46]=raw;bad[62]|=1<<31;bind(bad)
  assert decode_prepare_capsule(bad,source,boot)['prepare']['availability']=='UNKNOWN'
 old=words.copy();old[1]=2;bind(old);assert decode_prepare_capsule(old,source,boot)['prepare']['availability']=='UNAVAILABLE_SCHEMA2'
 for changed,src,b in [(words[:40]+[words[40]^1]+words[41:],source,boot),(words,'f'*64,boot),(words,source,9)]:
  try:decode_prepare_capsule(changed,src,b)
  except ValueError:pass
  else:raise AssertionError('Invalid capsule binding accepted')
 passed.append('decoder-CRC-binding-mode-trade')
 # Explicit verbose queue counter mode remains distinct from minimal mode.
 v=words.copy();v[40:63]=struct.unpack('<23I',(out/'verbose.words.bin').read_bytes());bind(v)
 assert decode_prepare_capsule(v,source,boot,verbose=True)['prepare']['success_counters']['state']=='ENABLED_VERBOSE'
 assert decode_prepare_capsule(v,source,boot)['prepare']['availability']=='UNKNOWN'
 passed.append('verbose-mode-compatibility')
 original=hooks.APP
 for gate in ['input','output','patch','modes']:
  with tempfile.TemporaryDirectory(prefix='ptob-prepare-negative-') as tmp:
   fake=Path(tmp);(fake/'cmake').mkdir()
   for n in ['controller-prepare-diag.json','controller-prepare-fault.patch']:shutil.copyfile(app/'cmake'/n,fake/'cmake'/n)
   f=fake/'cmake/controller-prepare-diag.json';m=json.loads(f.read_text())
   if gate=='input':m['input_sha256']='0'*64
   if gate=='output':m['output_sha256']['failure-only']='0'*64
   if gate=='modes':m['output_sha256'].pop('verbose')
   if gate=='patch':(fake/'cmake/controller-prepare-fault.patch').write_text('wrong')
   f.write_text(json.dumps(m));hooks.APP=fake
   try:
    try:hooks.generate(out/'backport/ll_sw/ull.c',fake/'generated')
    except AssertionError:pass
    else:raise AssertionError('Unexpected generation accepted '+gate)
   finally:hooks.APP=original
 passed.append('pinned-generation-negative-controls')
 diag=(app/'src/diagnostic.c').read_text();main=(app/'src/main.c').read_text()
 assert diag.index('r2_controller_diag_init();')<diag.index('phase=1;return 0;')
 assert diag.index('r2_controller_diag_init();')>diag.index('memset(scan_page,0,sizeof scan_page);')
 assert 'r2_controller_diag_init();' not in main
 assert main.index('r2_diag_init(&identity)')<main.index('r2_lora_init(')<main.index('r2_platform_start();')
 # Conditional streams remain tied to the explicit verbose macro.
 stream=main[main.index('#ifdef R2_CONTROLLER_DIAGNOSTICS\n    /* Ordinary images'):]
 assert stream.index('r2_timing_console(')<stream.index('#endif') and stream.index('r2_controller_diag_console(')<stream.index('#endif')
 passed.append('startup-publication-and-no-ordinary-stream')
finally:
 paths=['cmake/controller-prepare-diag.json','cmake/controller-prepare-fault.patch','tools/controller_diag_hooks.py','tools/controller_diag_tests.py','tools/controller_fault_decode.py','src/controller_diag.c','src/health.c','src/diagnostic.c','src/main.c','include/controller_diag.h','include/lora_watchdog.h','tests/controller_prepare_test.c','tests/controller_prepare_health_test.c']
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'limits':'Actual MFIFO and production bodies under sequential fake IRQ boundaries; no full LLL producer/preempt/dequeue lifecycle or physical latency proof. Phase faults conservatively return UNKNOWN.',
  'sources':{n:sha(app/n) for n in paths},'generated':{mode:sha(out/mode/'ull.c') for mode in ['failure-only','verbose'] if (out/mode/'ull.c').is_file()}},indent=2)+'\n')
print('PASS bounded failure-only prepare capture, exact modes, readiness and schema3 production bodies')
