#!/usr/bin/env python3
"""Source155 production diagnostic bodies, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,struct,subprocess,tempfile
from pathlib import Path
import ticker_backport
from controller_fault_decode import decode_pa,decode_capsule,decode_disconnect
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
z=a.deps/'zephyr';passed=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fn(s,name):
    m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',s,re.M)
    assert m,name
    return s[m.start():s.index('\n}',s.index('{',m.start()))+2]
flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
       '-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(app/'tests')]
def run(label,source,*args):
    with (out/(label+'.log')).open('x') as log:
        subprocess.run([*flags,str(source),'-o',str(out/label)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
        subprocess.run([str(out/label),*map(str,args)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
    passed.append(label)
try:
    generated=out/'generated';ticker_backport.generate(z,generated,True)
    ticker=(generated/'ticker/ticker.c').read_text();per=(generated/'ll_sw/ull_peripheral.c').read_text()
    (out/'fault_callback.inc').write_text(fn(per,'ticker_update_latency_cancel_op_cb')+'\n')
    (out/'fault_getter.inc').write_text(fn(ticker,'ptob_ticker_snapshot_locked')+'\n')
    for name in ['zephyr/fatal.h','zephyr/irq.h','zephyr/kernel.h','zephyr/timing/timing.h','cmsis_core.h','build_identity.h']:
        f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fake boundary. */\n')
    run('fault',app/'tests/controller_fault_diag_test.c',out/'capsule.bin')
    # The actual ordinary producer includes its pinned EXT wrapper and body.
    start=ticker.index('uint8_t ticker_update(')
    end=ticker.index('\n#endif /* CONFIG_BT_TICKER_UPDATE */',start)
    dispatch='\n'.join(fn(ticker,n) for n in ['ticker_job_op_cb','ticker_job_node_manage','ticker_job_list_manage'])
    dispatch+='\n'+fn(per,'ticker_update_latency_cancel_op_cb')+'\n'+ticker[start:end]+'\n'+fn(ticker,'ticker_stop')
    (out/'fault_dispatch.inc').write_text(dispatch+'\n')
    run('dispatch',app/'tests/controller_fault_dispatch_test.c')
    words=list(struct.unpack('<64I',(out/'capsule.bin').read_bytes()))
    source='0123456789abcdef'*4;boot=0x1122334455667788
    decoded=decode_capsule(words,source,boot)
    assert decoded=={'operation':0xc1590005,'result_signed':-2147483648,'result_raw':0x80000000,'ms':0,
        'kind':'LATENCY_CANCEL_FAILURE_CURRENT_SLOT','slot':5,'valid_slot':True,'update_mark':False,
        'disable_mark':False,'latency_cancel':True,'current_handle':5}
    for changed,src,b in [(words[:21]+[words[21]^1]+words[22:],source,boot),(words,'f'*64,boot),(words,source,9)]:
        try:decode_capsule(changed,src,b)
        except ValueError:pass
        else:raise AssertionError('CRC/source/boot negative accepted')
    assert decode_pa(0xc2f0ffff,-1,0)['kind']=='UNKNOWN_OPERATION_MARKER'
    assert decode_pa(12,-1,0)['result_raw']==0xffffffff
    assert decode_disconnect(29,0x1ffff,0xffffffff,0xfedcba98)=={'incarnation':0xffffffff,'access_address_le':0xfedcba98,'event_counter':65535,'role':1}
    assert decode_disconnect(30,0x07fffffe,0xffffffff,0xfffffeff)=={'incarnation':0xffffffff,'latency_event':65534,'ull_ref':255,'latency_cancel':True,'update_mark':True,'disable_mark':True,'req':255,'ack':254,'lazy_current':65535}
    assert decode_disconnect(31,0x00feff03,0xffffffff,0xffffffff)=={'incarnation':0xffffffff,'ticker_valid':True,'context_matches':True,'job_guard':255,'worker_trigger':254,'ticks_periodic':0xffffffff}
    passed.append('exact-decoder-CRC-binding')
    # Each manifest gate must fail closed independently; dependency bytes stay read-only.
    original_app=ticker_backport.APP
    for gate in ['patch','before','after','members']:
        with tempfile.TemporaryDirectory(prefix='ptob-diag-negative-') as tmp:
            fake=Path(tmp);(fake/'cmake').mkdir()
            for name in ['zephyr-91430.patch','ticker-backport.json','controller-fault-diag.patch','controller-fault-diag.json']:
                shutil.copyfile(app/'cmake'/name,fake/'cmake'/name)
            if gate=='patch':
                f=fake/'cmake/controller-fault-diag.patch';f.write_bytes(f.read_bytes()+b'!')
            else:
                f=fake/'cmake/controller-fault-diag.json';m=json.loads(f.read_text())
                if gate=='members':m['files'].pop(next(iter(m['files'])))
                else:m['files'][next(iter(m['files']))][gate]='0'*64
                f.write_text(json.dumps(m))
            ticker_backport.APP=fake
            try:
                try:ticker_backport.generate(z,fake/'generated',True)
                except AssertionError:pass
                else:raise AssertionError('Unexpected diagnostic source accepted: '+gate)
            finally:ticker_backport.APP=original_app
    passed.append('pinned-generation-negative-controls')
    # Disabled route keeps the existing upstream/policy output and excludes peripheral.
    plain=out/'without-diagnostics';ticker_backport.generate(z,plain)
    assert not (plain/'ll_sw/ull_peripheral.c').exists()
    assert 'ptob_latency_cancel_fault' not in (plain/'ticker/ticker.c').read_text()
    passed.append('diagnostics-disabled')
finally:
    sources=[app/p for p in ['src/controller_snapshot.c','src/controller_snapshot_body.inc','src/disconnect_diag.c',
        'include/controller_snapshot.h','tests/controller_fault_diag_test.c','tests/controller_fault_dispatch_test.c',
        'tools/controller_fault_diag_tests.py','tools/controller_fault_decode.py','tools/ticker_backport.py',
        'cmake/controller-fault-diag.patch','cmake/controller-fault-diag.json','cmake/zephyr-91430.patch','cmake/ticker-backport.json']]
    (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'physical_cause':'UNKNOWN',
        'limits':'Fake IRQ/LL layouts; reduced dequeue/node-update/release/reuse boundaries; prescribed schedules are conditional controls, not field proof.',
        'sources':{str(f.relative_to(app)):sha(f) for f in sources},
        'generated':{str(f.relative_to(out/'generated')):sha(f) for f in (out/'generated').rglob('*') if f.is_file()}},indent=2)+'\n')
print('PASS source155 actual diagnostic bodies and pinned generation; physical cause UNKNOWN')
