#!/usr/bin/env python3
"""Actual owner/matcher/control count0 recovery policy, no hardware. MIT Jonathan Yantis."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-source-blind',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
service=app/'src/rc_dynamic_service.c';transport=app/'src/rc_dynamic_transport.inc';texts={service:service.read_text(),transport:transport.read_text()}
def function(file,name):
 s=texts[file];m=re.search(r'^static [^\n;]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
names=['parent_path_usable','parent_source_wait','parent_receive_packet','parent_rebind_request','parent_scope_observed','parent_recovery_poll']
parts=[function(service,n)for n in names]
if a.legacy_source_blind:
 parts[names.index('parent_source_wait')]='static bool parent_source_wait(uint64_t tick){(void)tick;return false;}'
parts.extend(function(transport,n)for n in('lt_source_wait','lt_apply_read'))
(out/'rc_source_wait_under_test.h').write_text('\n'.join(parts)+'\n')
# Ensure the actual owner calls each tested body in its real receive/start path;
# source-hint scheduling must follow in-flight ACL handling and precede budgets.
s=texts[service];t=texts[transport]
assert 'if (parent_receive_packet(&packet,physical.tick,r.event,r.channel,'in s
assert 'parent_scope_observed(&c,tick);'in s
assert t.index('if (lt_conn) {',t.index('static void lt_poll('))<t.index('if(lt_source_wait(tick,need))')<t.index('if(!rc_admission_start_allowed(lt_start_history,tick))return;')
assert 'lt_deadline = tick + 32000000;'in t and 'lt_start_history[0]=lt_start_history[1];lt_start_history[1]=tick;'in t
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
units=[app/'tests/rc_source_wait_test.c',app/'src/rc_boundary.c',app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in('hmac.c','sha256.c','utils.c')]]
cmd=[os.environ.get('CC')or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with(out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with(out/'run.log').open('x')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'legacy_source_blind':a.legacy_source_blind,'scope':'ACTUAL_OWNER_RECOVERY_GATE_GROUP_CODEC_MATCHER_PENDING_READY_AND_START_BUDGET_NOT_RF','command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in[service,transport,app/'include/rc_admission_policy.h',Path(__file__).resolve(),*units]}},indent=2)+'\n')
print('PASS actual source-unavailable recovery scheduling')
