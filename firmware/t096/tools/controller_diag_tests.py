#!/usr/bin/env python3
"""Actual pinned prepare enqueue/MFIFO, scratch writer and schema3 fatal copy.
MIT. Copyright (c) 2026 Jonathan Yantis. No hardware/LLL integration claim.
"""
import argparse,hashlib,json,os,shutil,struct,subprocess,tempfile
from pathlib import Path
from ticker_backport import generate
import controller_diag_hooks as hooks
import controller_prepare_context as context
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
 generate(a.deps.resolve()/'zephyr',out/'backport',True)
 context.generate(a.deps.resolve()/'zephyr',out/'lll')
 def fn(body,name):
  import re
  m=re.search(r'^(?:static )?(?:inline )?(?:void|bool|int|uint32_t) '+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
  return body[m.start():body.index('\n}',body.index('{',m.start()))+2]
 lll=(out/'lll/lll.c').read_text();ticker=(out/'backport/ticker/ticker.c').read_text()
 (out/'prepare_context.inc').write_text(fn(ticker,'ptob_ticker_snapshot_locked')+'\n'+fn(lll,'is_done_sync')+'\n'+fn(lll,'r2_controller_diag_lll_context_locked')+'\n')
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
 d=decode_prepare_capsule(words,source,boot,context=True)
 assert d['schema']==3 and d['pa']['kind']=='LATENCY_CANCEL_FAILURE_CURRENT_SLOT'
 assert d['prepare']['availability']=='COMPLETE' and d['prepare']['success_counters']=={'state':'DISABLED','values':None}
 assert d['prepare']['copied_entries']==7 and d['prepare']['entries'][3]=={'callback':0x203,'param':0x103,'is_resume':True,'is_aborted':True}
 assert d['schema3_omits']==['irq_latency','interrupted_thread_frame']
 import zlib
 def bind(w):w[63]=zlib.crc32(struct.pack('<62I',*w[1:63]));return w
 for raw in [1,7,9]:
  bad=words.copy();bad[46]=raw;bad[62]|=1<<31;bind(bad)
  assert decode_prepare_capsule(bad,source,boot,context=True)['prepare']['availability']=='UNKNOWN'
 old=words.copy();old[1]=2;bind(old);assert decode_prepare_capsule(old,source,boot)['prepare']['availability']=='UNAVAILABLE_SCHEMA2'
 for changed,src,b in [(words[:40]+[words[40]^1]+words[41:],source,boot),(words,'f'*64,boot),(words,source,9)]:
  try:decode_prepare_capsule(changed,src,b,context=True)
  except ValueError:pass
  else:raise AssertionError('Invalid capsule binding accepted')
 passed.append('decoder-CRC-binding-mode-trade')
 # Explicit verbose queue counter mode remains distinct from minimal mode.
 v=words.copy();v[40:63]=struct.unpack('<23I',(out/'verbose.words.bin').read_bytes());bind(v)
 assert decode_prepare_capsule(v,source,boot,verbose=True)['prepare']['success_counters']['state']=='ENABLED_VERBOSE'
 assert decode_prepare_capsule(v,source,boot)['prepare']['availability']=='UNKNOWN'
 assert d['prepare']['context']['availability']=='CURRENT_CUT'
 legacy=words.copy();legacy[40:44]=[0]*4;bind(legacy)
 assert decode_prepare_capsule(legacy,source,boot)['prepare']['availability']=='COMPLETE'
 assert decode_prepare_capsule(legacy,source,boot,context=True)['prepare']['availability']=='UNKNOWN'
 assert decode_prepare_capsule(words,source,boot)['prepare']['availability']=='UNKNOWN'
 for index,mask in [(43,1<<28),(43,1<<27),(43,1<<26),(43,1<<22),(43,1<<23),(42,1)]:
  bad=words.copy();bad[index]^=mask;bind(bad)
  assert decode_prepare_capsule(bad,source,boot,context=True)['prepare']['availability']=='UNKNOWN'
 try:decode_prepare_capsule(words,source,boot,verbose=True,context=True)
 except ValueError:pass
 else:raise AssertionError('Mixed context/verbose mode accepted')
 # Full-width valid context plus every mayfly raw state; no activity inference.
 for state in range(4):
  full=words.copy();full[40:44]=[0xdeadbeef,0xfedcba91,0xfeffffff,0xd0000000|0x04000000|0x00c00000|(state<<20)|0x000f0000|0x02000000|0xfeff];bind(full)
  cut=decode_prepare_capsule(full,source,boot,context=True)['prepare']['context']
  assert cut['availability']=='CURRENT_CUT' and cut['ticker_lazy']==65535 and cut['ticker_req']==255 and cut['ticker_ack']==254
  assert cut['job_guard']==255 and cut['worker_trigger']==254 and cut['mayfly_state']==state
  assert cut['start_pending'] and cut['stop_pending'] and cut['expiry_pending']
 empty=words.copy();empty[40:63]=[0]*23;empty[62]=1<<30;bind(empty)
 assert decode_prepare_capsule(empty,source,boot,context=True)['prepare']['availability']=='NO_FAILURE_CAPTURED'
 partial=words.copy();partial[46]=7;partial[62]|=1<<31;bind(partial)
 assert decode_prepare_capsule(partial,source,boot,context=True)['prepare']['availability']=='UNKNOWN'
 passed.append('verbose-mode-compatibility')
 stock=(a.deps/'zephyr'/context.NAME).read_text()
 for name in ['lll_prepare_resolve','preempt','lll_done','preempt_ticker_start','preempt_ticker_stop']:
  assert fn(stock,name)==fn(lll,name),name
 cb=fn(lll,'preempt_ticker_cb').replace('r2_preempt_mfy','mfy')
 old_cb=fn(stock,'preempt_ticker_cb').replace('\tstatic memq_link_t link;\n\tstatic struct mayfly mfy = {0, 0, &link, NULL, preempt};\n','')
 assert cb==old_cb
 assert lll.count('static memq_link_t r2_preempt_link;')==1
 assert lll.count('static struct mayfly r2_preempt_mfy = {0, 0, &r2_preempt_link, NULL, preempt};')==1
 passed.append('unchanged-healthy-LLL-bodies-and-one-for-one-objects')
 # Compile the actual native configuration guard separately. Pointer width
 # is an explicit fixture type here; the native build checks the real ABI.
 guard=lll[lll.index('#if !defined(CONFIG_ARM)'):lll.index('BUILD_ASSERT(sizeof(uintptr_t)')]
 guard+=lll[lll.index('BUILD_ASSERT(sizeof(uintptr_t)'):lll.index('BUILD_ASSERT(sizeof(uintptr_t)')+len('BUILD_ASSERT(sizeof(uintptr_t) == 4, "Prepare context needs 32-bit addresses");')]
 prefix='#define BUILD_ASSERT(x,m) _Static_assert(x,m)\n'
 good=['CONFIG_ARM=1','CONFIG_BT_CTLR_LLL_PRIO=0','CONFIG_BT_CTLR_ULL_HIGH_PRIO=1','CONFIG_BT_CTLR_ULL_LOW_PRIO=1']
 with (out/'context-guards.log').open('x') as log:
  cases=[('supported',good,'unsigned int',True),('pointer-width',good,'unsigned long long',False),('not-ARM',good[1:],'unsigned int',False)]
  for macro in ['CONFIG_SMP','CONFIG_ZERO_LATENCY_IRQS','CONFIG_BT_CTLR_ZLI','CONFIG_BT_CTLR_LOW_LAT','CONFIG_BT_CTLR_LOW_LAT_ULL_DONE','R2_CONTROLLER_DIAGNOSTICS']:
   cases.append((macro,good+[macro+'=1'],'unsigned int',False))
  for macro,value in [('CONFIG_BT_CTLR_LLL_PRIO',1),('CONFIG_BT_CTLR_ULL_HIGH_PRIO',0),('CONFIG_BT_CTLR_ULL_LOW_PRIO',0)]:
   cases.append((macro,[x for x in good if not x.startswith(macro+'=')]+[macro+'='+str(value)],'unsigned int',False))
  for label,defines,word,expected in cases:
   cmd=[flags[0],'-std=c11','-x','c','-fsyntax-only',*['-D'+x for x in defines],'-']
   result=subprocess.run(cmd,input=prefix+'typedef '+word+' uintptr_t;\n'+guard,text=True,capture_output=True,timeout=30)
   log.write(label+' returncode='+str(result.returncode)+'\n'+result.stdout+result.stderr)
   assert (result.returncode==0)==expected,label
 passed.append('actual-configuration-guards')
 original_context_app=context.APP
 for gate in ['input','output','patch','format','schema']:
  with tempfile.TemporaryDirectory(prefix='ptob-context-negative-') as tmp:
   fake=Path(tmp);(fake/'cmake').mkdir()
   for name in ['controller-prepare-context.json','controller-prepare-context.patch']:shutil.copyfile(app/'cmake'/name,fake/'cmake'/name)
   path=fake/'cmake/controller-prepare-context.json';manifest=json.loads(path.read_text())
   if gate=='patch':(fake/'cmake/controller-prepare-context.patch').write_text('wrong')
   elif gate=='format':manifest['format_tag']=0
   elif gate=='schema':manifest['schema']=0
   else:manifest[gate+'_sha256']='0'*64
   path.write_text(json.dumps(manifest));context.APP=fake
   try:
    try:context.generate(a.deps/'zephyr',fake/'generated')
    except AssertionError:pass
    else:raise AssertionError('Unexpected LLL generation accepted '+gate)
   finally:context.APP=original_context_app
 passed.append('LLL-pinned-input-output-patch-format-negative-controls')
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
 paths=['cmake/controller-prepare-diag.json','cmake/controller-prepare-fault.patch','tools/controller_diag_hooks.py','tools/controller_prepare_context.py','cmake/controller-prepare-context.json','cmake/controller-prepare-context.patch','tools/controller_diag_tests.py','tools/controller_fault_decode.py','src/controller_diag.c','src/health.c','src/diagnostic.c','src/main.c','include/controller_diag.h','include/lora_watchdog.h','tests/controller_prepare_test.c','tests/controller_prepare_health_test.c']
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'limits':'Actual MFIFO and production bodies under sequential fake IRQ boundaries; no full LLL producer/preempt/dequeue lifecycle or physical latency proof. Phase faults conservatively return UNKNOWN.',
  'sources':{n:sha(app/n) for n in paths},'generated':{mode:sha(out/mode/'ull.c') for mode in ['failure-only','verbose'] if (out/mode/'ull.c').is_file()}},indent=2)+'\n')
print('PASS bounded failure-only prepare capture, exact modes, readiness and schema3 production bodies')
