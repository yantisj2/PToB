#!/usr/bin/env python3
"""Pinned production AUX admission/response branches and diagnostics; no RF.
Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from radio_capture_hooks import generate
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-hooks',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
generate(a.deps/'zephyr',out/'lll')
stock=a.deps/'zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_scan_aux.c'
source=stock if a.legacy_hooks else out/'lll/lll_scan_aux.c';body=source.read_text()
def function(name):
 m=re.search(r'^static [^\n;]*\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 end=m.end();depth=1
 while depth:depth+=(body[end]=='{')-(body[end]=='}');end+=1
 return body[m.start():end]
# Compile the unchanged initiating branch through request preparation; later
# scheduling/controller ownership remains with Zephyr, outside this fake.
m=re.search(r'^static int isr_rx_pdu\([^;]*?\)\s*\{',body,re.M);assert m
prefix=body[m.start():body.index('\t\t/* assert if radio packet ptr',m.end())]
prefix+='\t\treturn 0;\n#endif\n\t}\n\treturn -EINVAL;\n}\n'
m=re.search(r'^static void isr_rx_connect_rsp\([^;]*?\)\s*\{',body,re.M);assert m
response=body[m.start():body.index('\t/* No Rx or invalid PDU received */',m.end())]+'}\n'
(out/'aux_under_test.h').write_text(prefix+'\n'+function('isr_rx_connect_rsp_check')+'\n'+response+'\n'+function('isr_tx_connect_req')+'\n'+function('abort_cb'))
units=[app/'tests/initiator_aux_test.c',app/'src/initiator_diag.c']
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-but-set-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'legacy_hooks':a.legacy_hooks,'scope':'PINNED_PRODUCTION_AUX_ADMISSION_PREFIX_REQUEST_TX_ISR_RESPONSE_VALIDATOR_NOT_RF','command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [source,Path(__file__).resolve(),app/'tools/radio_capture_hooks.py',*units]}},indent=2)+'\n')
print((out/'test.log').read_text())
