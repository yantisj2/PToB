#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Production RC advertising gates."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
app=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-withdraw',action='store_true');a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False)
sources=[app/'src'/n for n in ['rc_dynamic_service.c','rc_dynamic_transport.inc','rc_unicast.c']]
def function(path,name):
    s=path.read_text();m=re.search(r'^(?:static )?[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
    start=m.start();brace=m.end()-1;end=brace+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
body='\n'.join(function(s,n) for s,n in
    [(sources[0],n) for n in ['precision_synced','downstream_ready','start','stop','advertising_withdraw','publication_demand_poll']]+
    [(sources[1],'lt_advertise'),(sources[2],'r2_uc_poll')] )
if a.legacy_withdraw:
    begin=body.index('static bool advertising_withdraw(');brace=body.index('{',begin);end=brace+1;depth=1
    while depth:depth+=(body[end]=='{')-(body[end]=='}');end+=1
    body=body[:begin]+'''static bool advertising_withdraw(uint64_t tick){
 if(!running||precision_synced(tick))return true;
 if(!stop())return false;sent_count=0;phase_admitted=start_prepared=awaiting_first_tx=false;
 lt_acquiring=false;restart_at=0;++train;return true;
}'''+body[end:]
(out/'rc_advertising_under_test.h').write_text(body)
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe',
     '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),
     str(app/'tests/rc_advertising_test.c'),'-o',str(out/'test')]
passed=False
try:
    subprocess.run(cmd,check=True,timeout=30);subprocess.run([str(out/'test')],check=True,timeout=10);passed=True
finally:
    (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,
        negative_old_withdraw=a.legacy_withdraw,
        production_functions=['precision_synced','downstream_ready','start','stop','advertising_withdraw','publication_demand_poll','lt_advertise','r2_uc_poll'],
        sources={str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in [*sources,app/'include/rc_transport_hold.h']}),indent=2)+'\n')
