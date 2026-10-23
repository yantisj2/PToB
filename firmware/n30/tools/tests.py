#!/usr/bin/env python3
"""Offline production-renderer, input, rotation and onboarding tests. No hardware."""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 cmd=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),str(APP/'tests/ui_test.cpp'),str(APP/'main/ui_render.cpp'),str(APP/'main/ui_model.cpp'),'-o',str(out/'ui-test')]
 subprocess.run(cmd,check=True,timeout=40);subprocess.run([out/'ui-test'],check=True,timeout=40)
 signal=[str(APP/'tests/signal_strength_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'signal-strength-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(signal,check=True,timeout=40);subprocess.run([out/'signal-strength-test'],check=True,timeout=10)
 for n30 in (False,True):
  exe=out/('n30-clock-prediction' if n30 else 'shared-clock-prediction')
  prediction=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-I'+str(APP.parent/'w12/main'),*(['-DPTOB_N30=1'] if n30 else []),str(APP/'tests/clock_prediction_test.cpp'),'-o',str(exe)]
  subprocess.run(prediction,check=True,timeout=40);subprocess.run([exe],check=True,timeout=10)
 precision=[str(APP/'tests/precision_ui_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'precision-ui-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(precision,check=True,timeout=40);subprocess.run([out/'precision-ui-test'],check=True,timeout=10)
 relative=[str(APP/'tests/relative_rate_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'relative-rate-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(relative,check=True,timeout=40);subprocess.run([out/'relative-rate-test'],check=True,timeout=10)
 dial=[str(APP/'tests/dial_hold_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'dial-hold-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(dial,check=True,timeout=40);subprocess.run([out/'dial-hold-test'],check=True,timeout=10)
 phase=[str(APP/'tests/phase_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'phase-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(phase,check=True,timeout=40);subprocess.run([out/'phase-test'],check=True,timeout=10)
 minimal=[str(APP/'tests/minimal_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'minimal-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(minimal,check=True,timeout=40);subprocess.run([out/'minimal-test'],check=True,timeout=10)
 continuity=[str(APP/'tests/saver_continuity_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'saver-continuity-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(continuity,check=True,timeout=40);subprocess.run([out/'saver-continuity-test'],check=True,timeout=10)
 lora=[str(APP/'tests/lora_policy_test.cpp') if x==str(APP/'tests/ui_test.cpp') else str(out/'lora-policy-test') if x==str(out/'ui-test') else x for x in cmd]
 subprocess.run(lora,check=True,timeout=40);subprocess.run([out/'lora-policy-test'],check=True,timeout=10)
 owner=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-I'+str(APP/'tests/lora_fakes'),'-I'+str(APP.parent/'w12/main'),'-I'+str(APP.parent/'w12/legacy'),str(APP/'tests/lora_owner_test.cpp'),'-o',str(out/'lora-owner-test')]
 subprocess.run(owner,check=True,timeout=40);subprocess.run([out/'lora-owner-test'],check=True,timeout=10)
 parent=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-DPTOB_N30=1','-I'+str(APP.parent/'w12/main'),str(APP/'tests/parent_fallback_test.cpp'),'-o',str(out/'parent-fallback-test')]
 subprocess.run(parent,check=True,timeout=40);subprocess.run([out/'parent-fallback-test'],check=True,timeout=10)
 recovery=[str(APP/'tests/recovery_budget_test.cpp') if x==str(APP/'tests/parent_fallback_test.cpp') else str(out/'recovery-test') if x==str(out/'parent-fallback-test') else x for x in parent]
 subprocess.run(recovery,check=True,timeout=40)
 for case in ('parent','lora'):subprocess.run([out/'recovery-test',case],check=True,timeout=10)
 adapter=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-DPTOB_N30=1','-I'+str(APP.parent/'w12/main'),'-I'+str(APP.parent/'w12/main/r2/r2-g1'),'-I'+str(APP.parent/'w12/main/r2/rc7/include'),'-I'+str(APP.parent/'w12/legacy'),'-I'+str(APP.parent/'w12/legacy/reference'),str(APP/'tests/adapter_test.cpp'),str(APP/'main/ui_model.cpp'),'-o',str(out/'adapter-test')]
 subprocess.run(adapter,check=True,timeout=40);subprocess.run([out/'adapter-test'],check=True,timeout=10)
 sync=[str(APP/'tests/sync_watchdog_adapter_test.cpp') if x==str(APP/'tests/adapter_test.cpp') else str(out/'sync-adapter-test') if x==str(out/'adapter-test') else x for x in adapter]
 subprocess.run(sync,check=True,timeout=40);subprocess.run([out/'sync-adapter-test'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/sync_watchdog_test.py','--out',out/'sync-watchdog'],check=True,timeout=80)
 subprocess.run([sys.executable,'-B',APP/'tests/inputs_test.py'],check=True)
 subprocess.run([sys.executable,'-B',APP/'tests/console_test.py'],check=True)
 display=(APP/'main/display.cpp').read_text()
 (out/'surface_trace_actual.inc').write_text('portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;\nn30::Model published;\nuint64_t now_us(){return uint64_t(read_clock());}\n'+display[display.index('struct SurfaceRecord {'):display.index('void frame_wake(')]+display[display.index('void n30_display_trace_poll(){'):display.index('bool display_init(){')])
 worker=[*cmd[:cmd.index(str(APP/'tests/ui_test.cpp'))],'-I'+str(out),'-I'+str(APP/'tests/console_fakes'),'-I'+str(APP.parent/'w12/tests/console_fakes'),'-I'+str(APP.parent/'w12/main'),str(APP/'tests/console_worker_test.cpp'),'-o',str(out/'console-worker-test')]
 subprocess.run(worker,check=True,timeout=40);subprocess.run([out/'console-worker-test',out/'actual-tx-log.txt'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/tx_capture_test.py','--wire',out/'actual-tx-log.txt'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/status_budget_test.py'],check=True,timeout=10)
 subprocess.run([sys.executable,'-B',APP/'tests/command_test.py','--out',out/'command-dispatch'],check=True,timeout=40)
 subprocess.run([sys.executable,'-B',APP/'tools/fine_tests.py','--out',out/'fine-shadow'],check=True,timeout=120)
 subprocess.run([sys.executable,'-B',APP/'tools/timing_tests.py','--out',out/'timing-diagnostics'],check=True,timeout=120)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','checks':['15min-actual-saver-residence-half-contrast-minimal-header-only-three-positions-PPS-two-edges-no-dark-interval-frames-wake-loss-grace-reset','one-saver-session-BLE-LOR-switches-unlocked-following-transfer-cache-reset-15min-boundary-and-one500ms-total-sync-loss-deadline','production-renderer-bounds-states','causal-signed-residual-trend-eight-second-baseline-scope-gap-reset-and-clock-independent-zero','actualN30supervisor300sno-time/RTC/continuous-reset/OFF/maintenance/unchangedW12-120s-fault','freshFOLLOW-and-availableLOR-deadlines-only-no-held-time-or-poll-renewal','N30failed-parent60sretryboundary/renewal/rebind/manual/lockrecovery/W12default','accumulated180sBLEloss/sustained180srecovery-no-flicker-starvation-AUTO/KEEPON/KEEPOFF','actualSX1262ownerfakes:noTX/cancel/CRC/malformed/partialcapture/DMAretention/stall','independentBLE-LoRadelta/drift-fullscope-and-expiry','UTC-phase-4Hz-8Hz-no-catchup-model-adjustments-and-shared-dashboard-saver-PPS-expiry','clock-first-changed-span-both-rotations-shadow-failure-repair','500ms-UTC-dial-frame-freeze-only-RMS-lock-live-and-saver-position-all-pages-boundary-wake-recovery','every-frame-timeout-budget-0-through-50000us','all-8192-pixels-both-rotations','clock-expiry-backward-time-fault','actual-W12-status-adapter-scope-permission-hold','four-pages-PRECISION-lock-scope-loss-expiry-RF-help-idle-wake-blank-boot-held-third-only-consumed-toggle-navigation-cancel','main-1.5s-one-shot-upstream-toggle-and-expiring-mailbox','BLE-RC1-RC2-source-and-three-position-T096-saver','activity-scope-and-disable','RC-only-no-GPS-inputs','atomic-shared-map-onboarding-rejections','normal-and-LoRa-ten-row-nonce-CRC-console-parser-strict-RX-only-schema','actual-ten-row-firmware-admission-and-UART-worker','maximum-width-receiver-relay-and-LoRa-diagnostics-8704-byte-budget','actual-command-dispatch-page-control-with-active-BLE-and-identity-ticket-maintenance-fences'],'hardware_access':False},indent=2)+'\n')
 print('PASS: offline N30 UI, state and onboarding checks')
if __name__=='__main__':main()
