#!/usr/bin/env python3
"""Exact UI diagnostic counters, due decisions and complete-record replay.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
# Exact pre323 production record stream for the fixed synthetic corpus below.
GOLDEN='8500865ae7a6d23d9c345d22f2f06d1ece51543790c1d31094c2ef71e3c37011'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-app',type=Path);a=p.parse_args()
 o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
 def manifest():return{str(p.relative_to(APP)):sha(p)for p in APP.rglob('*')if p.is_file()and p.suffix in('.c','.h','.inc','.py')and'__pycache__'not in p.parts}
 before=manifest();ui=(APP/'src/ui_service.c').read_text();diag=(APP/'src/diagnostic.c').read_text()
 marker='static unsigned logical_flags(const struct r2_ui_model *m,const struct r2_ui_sync_display *c,uint64_t now){'
 assert ui.count(marker)==1;(o/'ui_under_test.c').write_text(ui.replace(marker,marker+'++projected;'))
 oracle=(APP/'tests/ui_logic_due_pre323.h').read_text();(o/'baseline_due.h').write_text(oracle)
 def function(t,start,end):return t[t.index(start):t.index(end,t.index(start))]
 if a.baseline_app:
  old=(a.baseline_app/'src/ui_service.c').read_text()
  exact=function(old,'bool r2_ui_logic_due(','static uint32_t logical_signature(').strip().replace('r2_ui_logic_due','baseline_logic_due',1)
  assert oracle[oracle.index('bool baseline_logic_due('):].strip()==exact
 # Main is the only production consumer/producer; worker publication changes names only.
 calls={}
 for pth in (APP/'src').glob('*'):
  if pth.is_file()and pth.suffix in('.c','.inc'):
   t=pth.read_text()
   for name in ['r2_ui_logic_due','r2_ui_publish_owned','r2_ui_publish']:
    count=len(re.findall(r'\b'+name+r'\s*\(',t))
    if count:calls.setdefault(name,{})[pth.name]=count
 assert calls=={'r2_ui_logic_due':{'main.c':1,'ui_service.c':1},'r2_ui_publish_owned':{'main.c':1,'ui_service.c':2},'r2_ui_publish':{'main.c':1,'ui_service.c':1}},calls
 publish=function(ui,'void r2_ui_publish_owned(','void r2_ui_publish(')
 assert len(re.findall(r'\bmodel\s*=\s*',ui))==1 and 'model=next;'in publish
 assert len(re.findall(r'\bmodel_context\s*=(?!=)',ui))==2 and len(re.findall(r'\bmodel_hint\s*=(?!=)',ui))==1
 assert set(re.findall(r'memcpy\(model\.([A-Za-z_]+)',ui))=={'root_name','upstream_name','coordinator_name'}
 worker=function(ui,'static void worker(','void r2_ui_start(')
 assert not re.search(r'\br2_ui_(?:publish|publish_owned|logic_due)\s*\(',worker)
 assert 'r2_ui_snapshot('not in diag and diag.count('r2_ui_diag_snapshot(')==2
 assert 'snapshot(current,s,ui_max_frame_ms,ui_missed);last_service=*s;last_clock=*clock;'in diag
 for name in ['kernel.h','sys/atomic.h','random/random.h']:
  pth=o/'zephyr'/name;pth.parent.mkdir(parents=True,exist_ok=True);pth.write_text('/* Host fake boundary. */\n')
 (o/'build_identity.h').write_text('#define R2_SOURCE_SHA "'+'f'*64+'"\n')
 cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(o)]
 def run(name,args):
  r=subprocess.run(list(map(str,args)),capture_output=True,timeout=60);(o/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
 for variant,extra in [('plain',[]),('lora',['-DR2_LORA=1'])]:
  binary=o/variant
  objects=[]
  for name in ['ui_render','precision']:
   obj=o/(variant+'-'+name+'.o');objects.append(obj)
   run(variant+'-'+name+'-build',flags+extra+['-c',APP/'src'/(name+'.c'),'-o',obj])
  run(variant+'-build',flags+extra+['-DR2_UI_DIAG_WORK_FIXTURE=1','-include',APP/'tests/ui_service_fake.h',APP/'tests/ui_diag_work_test.c',*objects,'-o',binary]);run(variant,[binary])
  print((o/(variant+'.log')).read_text().strip())
 records={}
 versions=[('current',APP)]
 if a.baseline_app:versions.append(('baseline',a.baseline_app.resolve()))
 for name,app in versions:
  (o/'diag_under_test.c').write_text((app/'src/diagnostic.c').read_text())
  binary=o/('records-'+name)
  run(name+'-records-build',flags+['-DR2_LORA=1',APP/'tests/diag_record_work_test.c',APP/'src/core.c','-o',binary])
  with(o/(name+'-records.bin')).open('xb')as stream:
   result=subprocess.run([str(binary)],stdout=stream,stderr=subprocess.PIPE,timeout=60);(o/(name+'-records-run.log')).write_bytes(result.stderr);result.check_returncode()
  records[name]=sha(o/(name+'-records.bin'))
 if 'baseline'in records:assert records['current']==records['baseline'],records
 if GOLDEN!='PENDING_BASELINE_REPLAY':assert records['current']==GOLDEN,records
 else:assert a.baseline_app,'Initial golden requires exact prior source'
 assert before==manifest()
 external={str(a.baseline_app.resolve()/n):sha(a.baseline_app/n)for n in ['src/ui_service.c','src/diagnostic.c']}if a.baseline_app else{}
 (o/'receipt.json').write_text(json.dumps(dict(status='PASS_UI_DIAGNOSTIC_COUNTERS_AND_DUE_EXACT_RECORD_REPLAY',hardware=False,inputs=before,external_primary_sources=external,record_sha256=records['current'],baseline_record_sha256=records.get('baseline'),record_iterations=20000,whole_record_bytes_equal='baseline'in records,getter_and_due_cases=200000,production_callers=calls,record_format_unchanged=True,new_persistent_state_bytes=0),indent=2)+'\n')
 print('PASS20000 complete record replay iterations',records)
if __name__=='__main__':main()
