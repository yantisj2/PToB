#!/usr/bin/env python3
"""Production non-chain periodic/scanner AUX classification and cleanup tests.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,re,shutil,subprocess,tempfile
from pathlib import Path
from scan_aux_context import generate
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--cc',default=shutil.which('clang'))
    a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
    z=a.deps.resolve()/'zephyr';source=z/'subsys/bluetooth/controller/ll_sw/ull_scan_aux.c'
    m=generate(z,o/'generated');original=source.read_text();fixed=(o/'generated/ull_scan_aux.c').read_text()
    def bodies(s):
        names=['rx_release_put','sync_create_get','sync_iso_create_get','ull_scan_aux_setup','ull_scan_aux_release','ull_scan_aux_stop','done_disabled_cb','flush_safe','flush','aux_sync_partial','aux_sync_incomplete','ticker_op_cb']
        def fn(name):
            match=re.search(r'^(?:static |inline |int |void |bool |uint8_t |struct )[^;{}]*?\b'+name+r'\([^;]*?\)\n\{',s,re.M)
            assert match,name
            return s[match.start():s.index('\n}',match.end())+2]
        return '\n\n'.join(fn(n)for n in names)
    body=bodies(original)
    def site(anchor,assertion):
        assert body.count(anchor)==1
        return body[:body.index(assertion,body.index(anchor))].count('\n')+1
    sites=dict(sync_flush=site('const struct ll_sync_set *sync_set;\n\n\t\t\tLL_ASSERT(sync_lll);','LL_ASSERT(sync_lll);'),
        scan_flush=site('if (!IS_ENABLED(CONFIG_BT_CTLR_SYNC_PERIODIC) || aux->rx_last) {','LL_ASSERT(scan);'),
        scan_schedule=site('/* Switching to ULL scheduling to receive auxiliary PDUs */','LL_ASSERT(scan);'))
    (o/'expected-sites.h').write_text(''.join('#define EXPECT_'+k.upper()+'_LINE '+str(v)+'u\n'for k,v in sites.items()))
    pdu=z/'subsys/bluetooth/controller/ll_sw/pdu.h';text=pdu.read_text()
    def struct(name):
        start=text.index('struct '+name+' {');return text[start:text.index('} __packed;',start)+11]
    (o/'actual_pdu.inc').write_text('\n'.join(struct(n)for n in ['pdu_adv_ext_hdr','pdu_adv_com_ext_adv','pdu_adv_adi','pdu_adv_aux_ptr']))
    rows=[]
    for label,s in [('original',body),('fixed',bodies(fixed))]:
        inc=o/('actual-'+label+'.inc');inc.write_text(s)
        cmd=[a.cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-but-set-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-D'+label.upper()+'=1','-I'+str(o),str(APP/'tests/scan_aux_context_test.c'),'-o',str(o/label)]
        with(o/(label+'.compile.log')).open('w')as f:subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT,timeout=60)
        with(o/(label+'.run.log')).open('w')as f:subprocess.run([str(o/label)],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=30)
        rows.append(dict(label=label,command=cmd,body_sha256=sha(inc),log_sha256=sha(o/(label+'.run.log'))))
    # An unpinned dependency must fail generation without producing a source.
    with tempfile.TemporaryDirectory()as td:
        bad=Path(td);target=bad/m['file'];target.parent.mkdir(parents=True);target.write_text(original+'\n')
        try:generate(bad,bad/'output')
        except AssertionError:pass
        else:raise AssertionError('Changed dependency accepted')
        assert not(bad/'output/ull_scan_aux.c').exists()
    assert sha(source)==m['input_sha256']
    inputs=[Path(__file__),APP/'tests/scan_aux_context_test.c',APP/'tools/scan_aux_context.py',APP/'cmake/scan-aux-context.json',APP/'cmake/scan-aux-context.patch',source,pdu]
    (o/'receipt.json').write_text(json.dumps(dict(passed=True,hardware=False,manifest=m,inputs={str(x):sha(x)for x in inputs},rows=rows,original_exact_assertions=4,fixed_cases=8,changed_dependency_rejected=True,limits='Production function bodies and PDU declarations with host allocation, metadata, IRQ, ticker, mayfly and HCI boundaries; not full Zephyr concurrency, native ABI, RF or hardware trigger proof.'),indent=2)+'\n')
    print('PASS original4 exact assertions; fixed8 periodic/scanner cleanup; changed dependency rejected')
if __name__=='__main__':main()
