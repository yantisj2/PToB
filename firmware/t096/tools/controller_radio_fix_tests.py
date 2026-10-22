#!/usr/bin/env python3
"""Actual selected runtime bodies with explicit HAL/IRQ/mayfly boundaries.
Copyright(c)2026 Jonathan Yantis. MIT harness; generated source is Apache-2.0.
"""
import argparse,hashlib,json,os,re,shutil,subprocess,sys,tempfile
from pathlib import Path
import controller_radio_fixes as fixes
from ticker_backport import generate as backport
import controller_diag_hooks as diag
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
Z=a.deps.resolve()/'zephyr';O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);passed=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fn(s,name):
 m=re.search(r'^(?:static |inline |int |void |bool |uint32_t |struct )[^\n]*\b'+name+r'\([^;]*?\)\n\{',s,re.M);assert m,name
 return s[m.start():s.index('\n}',m.end())+2]
paths={n:Z/n for n in json.loads((APP/'cmake/controller-radio-fixes.json').read_text())['files']};before={n:sha(p)for n,p in paths.items()}
fixes.generate(Z,O/'generated');backport(Z,O/'backport',True);diag.generate(O/'backport/ll_sw/ull.c',O/'diag')
lll=(Z/'subsys/bluetooth/controller/ll_sw/nordic/lll/lll.c').read_text();common=(Z/'subsys/bluetooth/controller/ll_sw/lll_common.c').read_text();ull=(O/'diag/ull.c').read_text()
base='subsys/bluetooth/controller/ll_sw/nordic/'
ppi=(Z/base/'hal/nrf5/radio/radio_nrf5_ppi.h').read_text()
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(O),'-I'+str(Z/'subsys/bluetooth/controller/util'),'-I'+str(Z/base/'hal/nrf5/radio')]
def run(name,test,fixed):
 cmd=[*flags,*(['-DFIXED=1']if fixed else[]),str(APP/'tests'/test),'-o',str(O/name)]
 with(O/(name+'.log')).open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(O/name)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
 passed.append(name)
for fixed in [False,True]:
 label='fixed'if fixed else'original'
 conn=(O/'generated/lll_conn.c').read_text()if fixed else paths[base+'lll/lll_conn.c'].read_text()
 radio=(O/'generated/radio.c').read_text()if fixed else paths[base+'hal/nrf5/radio/radio.c'].read_text()
 helpers=['hal_radio_nrf_ppi_channels_enable','hal_radio_nrf_ppi_channels_disable','hal_radio_enable_on_tick_ppi_config_and_enable','hal_trigger_crypt_ppi_disable','hal_radio_sw_switch_disable','hal_radio_sw_switch_cleanup']
 (O/'actual_hal.inc').write_text('/* Selected Apache-2.0 source bodies. */\n'+'\n'.join(fn(ppi,n)for n in helpers)+'\n'+fn(radio,'radio_tmr_status_reset')+'\n'+fn(radio,'radio_disable')+'\n')
 run('hal-'+label,'controller_radio_hal_test.c',fixed)
 selected=[(lll,'is_done_sync'),(lll,'prepare_dequeue_iter_ready_get'),(ull,'ull_prepare_enqueue'),(ull,'ull_prepare_dequeue_get'),(ull,'ull_prepare_dequeue_iter'),(lll,'lll_prepare_resolve'),(common,'lll_resume'),(ull,'ull_prepare_dequeue'),(lll,'lll_done'),(lll,'preempt'),(lll,'resume_enqueue'),(conn,'lll_conn_central_is_abort_cb'),(conn,'lll_conn_peripheral_is_abort_cb'),(conn,'lll_conn_abort_cb'),(lll,'lll_isr_cleanup'),(conn,'isr_done')]
 (O/'actual_lifecycle.inc').write_text('/* Selected Apache-2.0 source bodies; no whole-controller simulation. */\n'+'\n'.join(fn(s,n)for s,n in selected)+'\n')
 run('lifecycle-'+label,'controller_radio_lifecycle_test.c',fixed)
 # Preserve exact per-control extracted bodies; later controls cannot overwrite.
 shutil.copyfile(O/'actual_hal.inc',O/('actual_hal-'+label+'.inc'));shutil.copyfile(O/'actual_lifecycle.inc',O/('actual_lifecycle-'+label+'.inc'))
# Configure the actual include against a source-only target, including both
# duplicate/missing-member negatives and the native/verbose versus exclusions.
cmake=shutil.which('cmake');assert cmake,'cmake required for actual selection guard'
for mode in ['normal','verbose','duplicate-radio','missing-conn','dc','pc','bsim','non-arm']:
 d=O/('cmake-'+mode);d.mkdir();sources=[str(paths[base+'hal/nrf5/radio/radio.c']),str(paths[base+'lll/lll_conn.c'])]
 if mode=='duplicate-radio':sources=[sources[0],sources[0]]
 if mode=='missing-conn':sources.pop()
 (d/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(scope C)\n'
  +'set(CONFIG_ARM '+('OFF'if mode=='non-arm'else'ON')+')\n'
  +'set(CONFIG_BOARD_NRF52_BSIM '+('ON'if mode=='bsim'else'OFF')+')\n'
  +'set(R2_DC_PROFILE '+('ON'if mode=='dc'else'OFF')+')\n'
  +'set(R2_PC_PROFILE '+('ON'if mode=='pc'else'OFF')+')\n'
  +'set(R2_CTLR_DIAGNOSTICS '+('ON'if mode=='verbose'else'OFF')+')\n'
  +'set(Python3_EXECUTABLE "'+sys.executable+'")\nset(ZEPHYR_BASE "'+str(Z)+'")\n'
  +'add_library(subsys__bluetooth__controller__ll_sw__nordic STATIC "'+'" "'.join(sources)+'")\n'
  +'include("'+str(APP/'cmake/controller-radio-fixes.cmake')+'")\n'
  +'get_target_property(selected subsys__bluetooth__controller__ll_sw__nordic SOURCES)\nfile(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${selected}")\n')
 with(d/'configure.log').open('x')as log:
  result=subprocess.run([cmake,'-S',str(d),'-B',str(d/'build')],stdout=log,stderr=subprocess.STDOUT,timeout=30)
 if mode in ['duplicate-radio','missing-conn']:
  assert result.returncode and 'Expected exactly one original' in (d/'configure.log').read_text(),mode
 else:
  assert result.returncode==0,mode
  selected=(d/'build/selected.txt').read_text();assert ('/r2_radio_fixes/'in selected)==(mode in ['normal','verbose']),mode
passed.append('actual-cmake-normal-verbose-exclusions-exact-membership')
original_app=fixes.APP
for gate in ['input','output','combined-patch','upstream-patch','upstream-commit','members']:
 with tempfile.TemporaryDirectory(prefix='ptob-radio-negative-')as tmp:
  fake=Path(tmp);(fake/'cmake').mkdir()
  for name in ['controller-radio-fixes.json','controller-radio-fixes.patch','zephyr-355648a.patch','zmk-10ba6d0.patch']:shutil.copyfile(APP/'cmake'/name,fake/'cmake'/name)
  m=json.loads((fake/'cmake/controller-radio-fixes.json').read_text())
  if gate=='combined-patch':(fake/'cmake/controller-radio-fixes.patch').write_text('wrong')
  elif gate=='upstream-patch':m['upstream'][0]['patch_sha256']='0'*64
  elif gate=='upstream-commit':m['upstream'][0]['commit']='0'*40
  elif gate=='members':m['files'].pop(next(iter(m['files'])))
  else:m['files'][next(iter(m['files']))][gate+'_sha256']='0'*64
  (fake/'cmake/controller-radio-fixes.json').write_text(json.dumps(m));fixes.APP=fake
  try:
   try:fixes.generate(Z,fake/'generated')
   except AssertionError:pass
   else:raise AssertionError('Unexpected radio source accepted '+gate)
  finally:fixes.APP=original_app
passed.append('pin-provenance-negative-controls')
assert before=={n:sha(p)for n,p in paths.items()}
(O/'receipt.json').write_text(json.dumps({'hardware':False,'passed':passed,'dependency_inputs_unchanged':before,'manifest_sha256':sha(APP/'cmake/controller-radio-fixes.json'),'sources':{str(p.relative_to(APP)):sha(p)for p in[APP/'tests/controller_radio_hal_test.c',APP/'tests/controller_radio_lifecycle_test.c',APP/'tools/controller_radio_fixes.py',APP/'tools/controller_radio_fix_tests.py']},'limits':['HAL register/event model does not measure peripheral transition latency or reproduce physical field initiating race','Actual callbacks/preempt/isr_done/lll_isr_cleanup/lll_done/MFIFO use fake immediate mayfly,ticker,prepare,explicit DISABLED IRQ delivery and HAL/LLCP/HCI boundaries','Active abort never completes synchronously; missing IRQ still reaches original full-queue assertion at bounded8th arrival','ZMK fork patch changes no-exchange preemption policy; forced/foreign/exchanged controls retained; physical connection progress and durability require native/field gates']},indent=2)+'\n')
print('PASS exact public runtime backports, original-negative HAL fence and real selected lifecycle/deferred-completion controls; physical cause UNKNOWN')
