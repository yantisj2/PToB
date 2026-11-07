#!/usr/bin/env python3
"""MIT. Jonathan Yantis2026. Actual RC first-check/WD functions, no hardware."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
A=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def function(text,name):
    start=text.index(name);i=text.index('{',start)+1;depth=1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path);a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
    inputs={str(p.relative_to(A)):sha(p)for p in A.rglob('*')if p.is_file()and p.suffix in('.c','.h','.inc','.py')and '__pycache__'not in p.parts};steps=[]
    src=(A/'src/rc_dynamic_service.c').read_text()
    (o/'check.inc').write_text(function(src,'static void check_at(')+'\n')
    (o/'snapshot.inc').write_text(function(src,'static void __attribute__((noinline)) check_snapshot(')+chr(10)+function(src,'static bool ui_snapshot_copy(')+chr(10)+function(src,'void r2_service_snapshot(')+chr(10))
    macro=src[src.index('#define RC_CHECK_BASE'):src.index('}while(0)')+len('}while(0)')]
    (o/'macro.inc').write_text(macro+'\n')
    config=function(src,'int r2_service_configure(')
    prefix=config[:config.index('  identity = *id;')].replace('r2_service_configure','configure_boundary')
    identity=config[config.index('  self_identity = strtoull'):config.index('  transmitter =')]
    clear='  atomic_set(&first_check_site,(atomic_val_t)r2_rc_check_next((uint32_t)atomic_get(&first_check_site)));\n  atomic_clear(&fault);'
    assert config.count(clear)==1
    assert 'return'not in config[config.index('  transmitter ='):config.index(clear)]
    status_init=config[config.index('  status = (struct r2_service_status)'):config.index('  snprintf(status.name')]
    assert '.rc_check_generation=(uint16_t)((uint32_t)atomic_get(&first_check_site)>>16)' in status_init
    assert 'k_mutex_lock(&state_lock,K_FOREVER);' in config[config.index(clear):config.index(status_init)]
    (o/'configure.inc').write_text(prefix+identity+clear+chr(10)+' k_mutex_lock(&state_lock,K_FOREVER);'+chr(10)+status_init+chr(10)+' k_mutex_unlock(&state_lock);'+chr(10)+' return 0;'+chr(10)+'}'+chr(10))
    for name in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/device.h','zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','cmsis_core.h','build_identity.h']:
        f=o/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* synthetic public platform boundary */\n')
    cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
    flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(q)for q in [o,A/'include',A/'protocol',A/'tests']]]
    def run(name,cmd):
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();steps.append(dict(name=name,command=cmd))
    status='INCOMPLETE'
    try:
        definitions=['-DR2_CHECK_BODY="'+str(o/'check.inc')+'"','-DR2_CHECK_MACRO="'+str(o/'macro.inc')+'"','-DR2_SNAPSHOT_BODY="'+str(o/'snapshot.inc')+'"','-DR2_CONFIGURE_BOUNDARY="'+str(o/'configure.inc')+'"']
        run('first-check-compile',[*flags,*definitions,str(A/'tests/rc_check_site_test.c'),'-o',str(o/'check')])
        for i in range(1,11):run('first-check-'+str(i),[str(o/'check'),str(i)])
        if a.baseline:
            baseline=(a.baseline/'src/rc_dynamic_service.c').read_text()
            (o/'baseline-check.inc').write_text(function(baseline,'static void check_at(')+'\n')
            base_defs=[x for x in definitions if not x.startswith('-DR2_CHECK_BODY=')]+['-DR2_CHECK_BODY="'+str(o/'baseline-check.inc')+'"']
            run('baseline313-compile',[*flags,*base_defs,str(A/'tests/rc_check_site_test.c'),'-o',str(o/'baseline313')])
            result=subprocess.run([str(o/'baseline313'),'2'],capture_output=True,text=True,timeout=30)
            (o/'baseline313-policy-negative.log').write_text(result.stdout+result.stderr)
            assert result.returncode and 'atomic_get(&fault)==-EIO' in result.stderr
            steps.append(dict(name='actual313-suppresses-fault-until-site-winner-resumes',expected_failure=True))
        run('watchdog-compile',[*flags,str(A/'tests/rc_check_watchdog_test.c'),str(A/'src/watchdog_policy.c'),'-o',str(o/'watchdog')])
        run('watchdog',[str(o/'watchdog')])
        bad=o/'range.c';bad.write_text('#include "rc_check_site.h"\nstatic void check_at(bool o,const char *w,unsigned s){(void)o;(void)w;(void)s;}\n'+macro+'\n#line 3000\nvoid bad(void){check(false,"outside");}\n')
        r=subprocess.run([*flags,'-fsyntax-only',str(bad)],capture_output=True,text=True,timeout=30);(o/'range-negative.log').write_text(r.stdout+r.stderr);assert r.returncode and 'source domain exhausted'in r.stderr
        steps.append(dict(name='compile-refuses-out-of-domain-source',expected_failure=True))
        assert all(sha(A/n)==h for n,h in inputs.items());status='PASS'
    finally:
        (o/'receipt.json').write_text(json.dumps(dict(status=status,hardware=False,inputs=inputs,steps=steps,
            limits=['Real C11 atomics/pthreads and exact production check/snapshot/WD bodies. Kernel/HCI/identity predicates are synthetic public boundaries.',
            'Configure test uses the original admission/join/reset gates, exact identity gate and exact provenance advance/fault clear; unrelated initialization assignments are not executed.',
            'Native TU/4B/callsite compiler map and complete final image/resource proof remain separate.']),indent=2)+'\n')
    print(status)
if __name__=='__main__':main()
