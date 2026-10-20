#!/usr/bin/env python3
"""Offline production-renderer, input, rotation and onboarding tests. No hardware."""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cmd=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),str(APP/'tests/ui_test.cpp'),str(APP/'main/ui_render.cpp'),str(APP/'main/ui_model.cpp'),'-o',str(out/'ui-test')]
 subprocess.run(cmd,check=True,timeout=40);subprocess.run([out/'ui-test'],check=True,timeout=40)
 dial=[str(APP/'tests/dial_hold_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'dial-hold-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(dial,check=True,timeout=40);subprocess.run([out/'dial-hold-test'],check=True,timeout=10)
 adapter=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-I'+str(APP.parent/'w12/main'),'-I'+str(APP.parent/'w12/main/r2/r2-g1'),'-I'+str(APP.parent/'w12/main/r2/rc7/include'),'-I'+str(APP.parent/'w12/legacy'),'-I'+str(APP.parent/'w12/legacy/reference'),str(APP/'tests/adapter_test.cpp'),str(APP/'main/ui_model.cpp'),'-o',str(out/'adapter-test')]
 subprocess.run(adapter,check=True,timeout=40);subprocess.run([out/'adapter-test'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/inputs_test.py'],check=True)
 subprocess.run([sys.executable,'-B',APP/'tests/console_test.py'],check=True)
 worker=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-I'+str(APP/'tests/console_fakes'),'-I'+str(APP.parent/'w12/tests/console_fakes'),'-I'+str(APP.parent/'w12/main'),str(APP/'tests/console_worker_test.cpp'),'-o',str(out/'console-worker-test')]
 subprocess.run(worker,check=True,timeout=40);subprocess.run([out/'console-worker-test'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/status_budget_test.py'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/command_test.py','--out',out/'command-dispatch'],check=True,timeout=40)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','checks':['production-renderer-bounds-states','500ms-main-dial-freeze-exact-boundary-recovery-expiry-hidden-transfer-reset','every-frame-timeout-budget-0-through-50000us','all-8192-pixels-both-rotations','clock-expiry-backward-time-fault','actual-W12-status-adapter-scope-permission-hold','two-pages-RF-help-idle-wake-blank-boot-held','main-1.5s-one-shot-upstream-toggle-and-expiring-mailbox','BLE-RC1-RC2-source-and-three-position-T096-saver','activity-scope-and-disable','RC-only-no-GPS-inputs','atomic-shared-map-onboarding-rejections','ten-row-nonce-CRC-console-parser','actual-ten-row-firmware-admission-and-UART-worker','maximum-width-receiver-and-relay-8704-byte-budget','actual-command-dispatch-page-control-with-active-BLE-and-identity-ticket-maintenance-fences'],'hardware_access':False},indent=2)+'\n')
 print('PASS: offline N30 UI, state and onboarding checks')
if __name__=='__main__':main()
