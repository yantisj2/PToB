#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Offline production MTU callback."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=app/'src/rc_dynamic_transport.inc';s=source.read_text();start=s.index('static void lt_mtu_done(');end=s.index('static void lt_written(',start)
marker=s.index('static uint8_t gm_located(');marker_end=s.index('static void gm_sent(',marker)
(out/'rc_mtu_under_test.h').write_text(s[start:end]+s[marker:marker_end])
cmd=[os.environ.get('CC')or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(app/'tests/rc_mtu_test.c'),'-o',str(out/'test')]
passed=False
try:
 with(out/'test.log').open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
  tz_cmd=cmd.copy();tz_cmd.insert(1,'-DR2_TZ_WIRE=1');tz_cmd[-1]=str(out/'tz-test')
  subprocess.run(tz_cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'tz-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,command=cmd,sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in [source,app/'tests/rc_mtu_test.c',Path(__file__).resolve()]}),indent=2)+'\n')
print('PASS actual RC MTU callback regression')
