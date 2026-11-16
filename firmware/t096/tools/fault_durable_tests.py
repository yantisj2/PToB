#!/usr/bin/env python3
"""Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
Production boot/fault retention and append bodies at explicit fake driver/reset
boundaries. No physical SRAM survival, NVMC timing or hardware acceptance claim.
"""
import argparse,copy,hashlib,importlib.util,json,os,struct,subprocess,sys,zlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    names=['src/fault_store.c','src/fault_boot.c','src/watchdog.c','src/watchdog_policy.c','src/health.c',
        'src/diagnostic.c','src/main.c','src/core.c','include/core.h','tests/core_test.c','include/fault_store.h','include/fault_boot.h','include/crash_record.h',
        'include/watchdog.h','include/health.h','tests/fault_store_test.c','tests/fault_boot_test.c',
        'tests/fault_durable_watchdog_test.c','tests/fault_durable_crash_test.c','tests/watchdog_test.c','tests/usb_observation_test.c',
        'tests/fault_retention_crash_test.c','tests/health_fake.h','tests/zephyr_fake.h',
        'tools/fault_context_decode.py','tools/crash_decode.py','tools/inspect_build.py','tools/fault_durable_tests.py']
    inputs={n:sha(APP/n)for n in names};passed=[]
    for n in ('zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/device.h',
        'zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','zephyr/drivers/flash.h','zephyr/fatal.h',
        'zephyr/irq.h','zephyr/timing/timing.h','cmsis_core.h','build_identity.h','ptob_device.h'):
        f=out/n;f.parent.mkdir(parents=True,exist_ok=True)
        f.write_text('#define PTOB_DEVICE_ID "123456789abcdef0"\n'if n=='ptob_device.h'else'/* synthetic public API boundary */\n')
    boot=out/'boot';boot.mkdir()
    for n in ('diagnostic.h','health.h','watchdog.h'):
        (boot/n).write_text('/* declared synthetic API in test before actual production body */\n')
    cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else'clang')
    flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
        '-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-I'+str(out),'-I'+str(APP/'include'),'-I'+str(APP/'protocol')]
    def run(label,command):
        with(out/(label+'.log')).open('x')as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=45)
        passed.append(label)
    try:
        for name,extras in [('fault_store_test',[APP/'src/fault_store.c']),
            ('usb_observation_test',[APP/'src/watchdog_policy.c']),
            ('fault_durable_watchdog_test',[APP/'src/watchdog_policy.c']),('fault_durable_crash_test',[]),
            ('fault_boot_test',[APP/'src/fault_store.c']),('core_test',[APP/'src/core.c'])]:
            binary=out/name;caseflags=[flags[0],*(['-I'+str(boot)]if name=='fault_boot_test'else[]),*(['-DR2_FAULT_DURABLE=1']if name=='core_test'else[]),*flags[1:]]
            run(name+'-compile',[*caseflags,str(APP/'tests'/(name+'.c')),*map(str,extras),'-o',str(binary)])
            run(name,[str(binary),*([str(out/'cpu.bin')]if name=='fault_store_test'else[str(out/'usb-retained.bin')]if name=='usb_observation_test'else[str(out/'wd.bin'),str(out/'bare.bin'),str(out/'usb.bin'),str(out/'usb-retained.bin')]if name=='fault_boot_test'else[])])
        sys.path.insert(0,str(APP/'tools'))
        cpu=module('durable_cpu_decode',APP/'tools/crash_decode.py')
        wd=module('durable_wd_decode',APP/'tools/fault_context_decode.py')
        d,_,_=cpu.decode((out/'cpu.bin').read_bytes());assert d['storage']=='bank0_flash'and d['crashed_boot']=='0000000000000001'
        d=wd.decode((out/'wd.bin').read_bytes())
        assert d['kind']=='WATCHDOG_CONTEXT'and not d['cpu_exception_frame']and d['originating_source']=='UNKNOWN'and d['cause']=='CONTROL_STALL'
        bare=wd.decode((out/'bare.bin').read_bytes());assert bare['kind']=='BARE_HARDWARE_RESET'and bare['origin_boot']=='UNKNOWN'and bare['observed_reset_boot']=='0000000000000002'and bare['hardware_reset_flags']==16 and bare['cause']=='UNKNOWN'and not bare['cpu_exception_frame']
        usb=wd.decode((out/'usb.bin').read_bytes())
        assert usb['kind']=='USB_OBSERVATION'and not usb['software_terminal_decision']and usb['cause']=='USB_OBSERVATION'and not usb['cpu_exception_frame']
        assert usb['usb_observation']['hardware_lowpower']and usb['usb_observation']['software_bus_suspend']and usb['usb_observation']['observed_uptime_ms']==300
        for value in (1,2,7,16,255,0xffffffff):
            raw=bytearray((out/'usb.bin').read_bytes());struct.pack_into('<I',raw,68*4,value)
            struct.pack_into('<I',raw,97*4,zlib.crc32(raw[260:388]));struct.pack_into('<I',raw,126*4,zlib.crc32(raw[:504]))
            observed=wd.decode(bytes(raw));assert observed['kind']=='USB_OBSERVATION'and not observed['software_terminal_decision']
        rejects=0
        for i in range(128):
            raw=bytearray((out/'wd.bin').read_bytes());raw[i*4]^=1
            try:wd.decode(bytes(raw))
            except(AssertionError,UnicodeDecodeError):rejects+=1
            else:raise AssertionError(('decoder admitted corrupt word',i))
        assert rejects==128
        for i in range(128):
            raw=bytearray((out/'usb.bin').read_bytes());raw[i*4]^=1
            try:wd.decode(bytes(raw))
            except(AssertionError,UnicodeDecodeError):pass
            else:raise AssertionError(('USB observation decoder admitted corrupt word',i))
        passed.append('independent-CPU-WD-USB-decoders256-corruption-negatives')
        m=module('durable_inspector',APP/'tools/inspect_build.py')
        values=[('r2_watchdog_capsule',136,'noinit'),('r2_crash_record',512,'noinit'),('r2_fault_capsule',256,'bss'),('uf2_intent',88,'noinit'),('r2_watchdog_pending',136,'noinit')]
        symbols={name:[dict(size=size,section=section,type='SHT_NOBITS',address=0x20020000+i*1024)]for i,(name,size,section)in enumerate(values)}
        d=m.fault_only_retention(True,(0,15,309),symbols)
        assert d['bytes']==784 and d['flash_persistence']and not d['operational_restore']
        rejects=0
        for name in symbols:
            for key,value in [('size',symbols[name][0]['size']+4),('section','data'),('type','SHT_PROGBITS'),('address',0x20040000)]:
                bad=copy.deepcopy(symbols);bad[name][0][key]=value
                try:m.fault_only_retention(True,(0,15,309),bad)
                except ValueError:rejects+=1
                else:raise AssertionError((name,key))
            for value in([],symbols[name]*2):
                bad=copy.deepcopy(symbols);bad[name]=value
                try:m.fault_only_retention(True,(0,15,309),bad)
                except ValueError:rejects+=1
                else:raise AssertionError(name)
        assert rejects==30;passed.append('exact784B-retention-contract30-negatives')
        # Counterfactuals isolate pre-feature acquisition and the two critical
        # priorities. Production bodies remain otherwise unchanged.
        body=(APP/'src/watchdog.c').read_text()
        edits={
            'no-observation-acquisition':('usb_observation(usb,usb_at);','(void)usb_at;'),
            'no-pending-fault-priority':('if(pending&&previous[0]==R2_USB_OBSERVATION_MAGIC&&(current_terminal||hard_reset))pending=false;','/* old first-pending observation hides later fault */'),
            'no-terminal-commit-guard':('if(!atomic_get(&reset_reason)&&r2_watchdog_capsule[0]!=WD_MAGIC){','if(!atomic_get(&reset_reason)||1){'),
        }
        for label,(old,new) in edits.items():
            assert body.count(old)==1,label
            changed=out/(label+'.c');changed.write_text(body.replace(old,new))
            binary=out/label
            run(label+'-compile',[*flags,'-DR2_WATCHDOG_BODY="'+str(changed)+'"',str(APP/'tests/usb_observation_test.c'),str(APP/'src/watchdog_policy.c'),'-o',str(binary)])
            q=subprocess.run([str(binary),*(['require-capture']if label=='no-observation-acquisition'else[])],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
            (out/(label+'.log')).write_text(q.stdout)
            assert q.returncode==42 if label=='no-observation-acquisition'else q.returncode!=0 and'assert'in q.stdout.lower(),(label,q.returncode,q.stdout)
            passed.append(label+'-expected-failure')
        assert all(sha(APP/n)==digest for n,digest in inputs.items())
    finally:
        (out/'receipt.json').write_text(json.dumps(dict(status='PASS'if len(passed)==20 else'INCOMPLETE',
            cases=passed,hardware=False,inputs=inputs,limits=[
            'Fake drivers model failures/byte cuts; actual pinned pre-radio driver and native resources require separate checks.',
            'Original-image admission does not guarantee physical cross-image SRAM/address survival.',
            '500ms transaction budget is checked around driver calls; blocked NVMC hardware is ultimately bounded by existing hardware watchdog, not mock timing.',
            'No physical fault injection, flash operation, core-dump capture or accuracy claim.']),indent=2)+'\n')
    print('PASS boot-only durable append, original-provenance pending lifecycle, sealed owner/readout, independent decoders and exact allocation contract')
if __name__=='__main__':main()
