#!/usr/bin/env python3
"""Actual pinned USB transfer/CDC function regressions; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
from usb_cdc_recovery import generate
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def functions(source,names):
    rows=[]
    for name in names:
        match=re.search(r'^[^\n;{}]*\b'+name+r'\([^;{}]*\)\n\{',source,re.M)
        assert match,name;rows.append(source[match.start():source.index('\n}',match.end())+2])
    return '\n\n'.join(rows)+'\n'
def main():
    p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
    z=a.deps.resolve()/'zephyr';manifest=generate(z,o/'generated');rows=[];cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
    for label in ('original','fixed'):
        for kind,leaf,fixture in [('transfer','usb_transfer.c','usb_transfer_recovery_test.c'),('cdc','cdc_acm.c','usb_cdc_recovery_test.c')]:
            entry=next(e for e in manifest['files']if e['leaf']==leaf);source=(z/entry['file'] if label=='original'else o/'generated'/leaf).read_text()
            if kind=='transfer':
                start=source.index('struct usb_transfer_data {');types=source[start:source.index('\n};',start)+3]+'\nstatic struct usb_transfer_data ut_data[CONFIG_USB_MAX_NUM_TRANSFERS];\n'
                body=types+functions(source,['usb_ep_get_transfer','usb_transfer_is_busy','usb_transfer_work','usb_transfer_ep_callback','usb_transfer','usb_cancel_transfer'])
                macro='R2_USB_TRANSFER_BODY'
            else:
                names=['cdc_acm_write_cb','tx_work_handler','cdc_acm_read_cb']+(['cdc_acm_rearm_idle']if label=='fixed'else[])+['cdc_acm_irq_tx_enable','cdc_acm_irq_tx_disable','cdc_acm_irq_rx_enable','cdc_acm_irq_rx_disable']
                body=functions(source,names);macro='R2_CDC_BODY'
            inc=o/(label+'-'+kind+'.inc');inc.write_text(body);binary=o/(label+'-'+kind)
            command=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-D'+label.upper()+'=1','-D'+macro+'="'+str(inc)+'"',str(APP/'tests'/fixture),'-o',str(binary)]
            with(o/(label+'-'+kind+'.compile.log')).open('w')as f:subprocess.run(command,check=True,stdout=f,stderr=subprocess.STDOUT)
            with(o/(label+'-'+kind+'.run.log')).open('w')as f:subprocess.run([str(binary)],check=True,stdout=f,stderr=subprocess.STDOUT)
            rows.append(dict(label=label,kind=kind,command=command,body_sha256=sha(inc)))
    with tempfile.TemporaryDirectory()as d:
        bad=Path(d)
        for entry in manifest['files']:
            f=bad/entry['file'];f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes((z/entry['file']).read_bytes()+b'\n')
        try:generate(bad,bad/'output')
        except AssertionError:pass
        else:raise AssertionError('Unpinned dependency accepted')
        assert not(bad/'output').exists()
    for entry in manifest['files']:assert sha(z/entry['file'])==entry['input_sha256']
    inputs=[Path(__file__),APP/'tools/usb_cdc_recovery.py',APP/'cmake/usb-cdc-recovery.patch',APP/'cmake/usb-cdc-recovery.json',APP/'tests/usb_transfer_recovery_test.c',APP/'tests/usb_cdc_recovery_test.c']
    (o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_PINNED_USB_RECOVERY_FUNCTIONS',hardware=False,rows=rows,manifest=manifest,inputs={str(f):sha(f)for f in inputs},changed_dependency_rejected=True,limits='Actual production function bodies with synthetic USB controller/workqueue/semaphore/ring boundaries. Not a full Zephyr concurrency or hardware causal proof; native build and close/reopen hardware checks remain required.'),indent=2)+'\n')
    print('PASS actual original counterexamples and fixed transfer/CDC ownership/start/reopen cases; SDK unchanged')
if __name__=='__main__':main()
