#!/usr/bin/env python3
"""Sanitized production watchdog/policy tests; never opens hardware."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
for name in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/sys/reboot.h','zephyr/device.h','zephyr/drivers/watchdog.h','zephyr/drivers/hwinfo.h','cmsis_core.h','build_identity.h']:
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public boundary supplied by fixture. */\n')
flags=[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
passed=False
try:
 with (out/'watchdog.log').open('x') as log:
  subprocess.run([*flags,str(app/'tests/watchdog_test.c'),str(app/'src/watchdog_policy.c'),'-o',str(out/'watchdog-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'watchdog-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([*flags,'-DR2_LORA=1',str(app/'tests/watchdog_test.c'),str(app/'src/watchdog_policy.c'),'-o',str(out/'watchdog-lora-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'watchdog-lora-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'profile.log').open('x') as log:
  subprocess.run([*flags,str(app/'tests/watchdog_profile_test.c'),str(app/'protocol/group_auth.c'),'-I'+str(a.deps/'modules/crypto/tinycrypt/lib/include'),*[str(a.deps/'modules/crypto/tinycrypt/lib/source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'profile-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'profile-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'auto-profile.log').open('x') as log:
  command=[*flags,str(app/'tests/rc_auto_profile_test.c'),str(app/'protocol/group_auth.c'),'-I'+str(a.deps/'modules/crypto/tinycrypt/lib/include'),*[str(a.deps/'modules/crypto/tinycrypt/lib/source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'auto-profile-test')]
  subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'auto-profile-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'physical':'UNKNOWN','sources':{str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [app/'src/watchdog.c',app/'src/watchdog_policy.c',app/'include/watchdog_policy.h',app/'tests/watchdog_test.c',Path(__file__).resolve()]}},indent=2)+'\n')
print('PASS production watchdog bodies with ASan/UBSan; physical watchdog/reset timing UNKNOWN')
