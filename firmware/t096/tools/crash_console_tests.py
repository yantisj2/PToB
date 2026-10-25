#!/usr/bin/env python3
"""MIT. Copyright(c)2026 Jonathan Yantis. Actual parser/reply crash-chunk surface."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);app=Path(__file__).resolve().parents[1];passed=[]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs=[Path(__file__),*[app/n for n in('src/main.c','src/core.c','include/core.h','include/health.h','include/diagnostic.h','tests/core_test.c','tests/reply_test.c','tests/zephyr_fake.h')]];bound={str(p):sha(p)for p in inputs}
for n in('kernel.h','device.h','drivers/uart.h','usb/usb_device.h','drivers/gpio.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','drivers/clock_control.h','drivers/clock_control/nrf_clock_control.h','sys/onoff.h','drivers/spi.h','drivers/sensor.h','drivers/pwm.h','drivers/flash.h'):
 q=out/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Fixture declarations force-included. */\n')
(out/'build_identity.h').write_text('#define R2_VERSION "0.15.180"\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#define R2_CHILD_COMMIT "'+'f'*40+'"\n#define R2_SOURCE_DIRTY 0\n')
(out/'r2_enrollment.h').write_text('static const struct r2_enrollment r2_enrollment={0};\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else'clang');flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol')]
def run(label,cmd):
 with(out/(label+'.log')).open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed.append(label)
try:
 run('core-compile',[*flags,'-DR2_GM7_NATIVE=1',str(app/'tests/core_test.c'),str(app/'src/core.c'),'-o',str(out/'core')]);run('core',[str(out/'core')])
 run('reply-compile',[*flags,'-DR2_NATIVE_BLE=1','-include',str(app/'tests/zephyr_fake.h'),str(app/'tests/reply_test.c'),str(app/'src/core.c'),'-o',str(out/'reply')]);run('reply',[str(out/'reply')])
 assert all(sha(Path(p))==h for p,h in bound.items())
finally:(out/'receipt.json').write_text(json.dumps(dict(passed=len(passed)==4,hardware=False,class_='ACTUAL_CORE_AND_MAIN_REPLY_FAKE_KERNEL_BOUNDARY',cases=passed,inputs=bound,coverage=['legacy00ff,new0100..0107,reject0108 parser','actualmain boundedresult/hex/fullCRCframe legacy255 andretained263 ENOENT','existingmaximumwidth responses andcoredeadlines'],limits=['Hostfakekernel/console; not actualCPU exception orUSB/hardware acceptance']),indent=2)+'\n')
print('PASS actual crash parser/reply surface')
