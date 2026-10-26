#!/usr/bin/env python3
"""Focused actual C core/CDC adapter regressions, using ASan and UBSan; no hardware."""
from pathlib import Path
import os
import argparse
p=argparse.ArgumentParser();p.add_argument("--deps",type=Path,required=True);p.add_argument("--out",type=Path,help="retain exact logs/binaries/receipts in a fresh directory");args=p.parse_args()
import shutil
import subprocess
import sys
import tempfile
from contextlib import nullcontext
APP=Path(__file__).resolve().parents[1]
if args.out: args.out.resolve().mkdir(parents=True,exist_ok=False)
with (nullcontext(str(args.out.resolve())) if args.out else tempfile.TemporaryDirectory(prefix='t096-r2-test-')) as directory:
    out=Path(directory)
    compiler=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang'))
    subprocess.run([sys.executable,str(APP/'tools/gm7_tests.py'),'--deps',str(args.deps),'--out',str(out/'gm7')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/gm_utc_tests.py'),'--deps',str(args.deps),'--out',str(out/'gm-utc')],env={**os.environ,'CC':compiler},check=True,timeout=90)
    subprocess.run([sys.executable,str(APP/'tools/gnss_diagnostic_tests.py'),'--deps',str(args.deps),'--out',str(out/'gnss-diagnostics')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/lora_rc8_tests.py'),'--deps',str(args.deps),'--out',str(out/'rc8')],
                   env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/lora_source_owner_tests.py'),'--deps',str(args.deps),'--out',str(out/'lora-source-owner')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/lora_runtime_tests.py'),'--deps',str(args.deps),'--out',str(out/'lora-runtime')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/lora_init_gate_tests.py'),'--deps',str(args.deps),'--out',str(out/'lora-init-gate')],env={**os.environ,'CC':compiler},check=True,timeout=90)
    subprocess.run([sys.executable,str(APP/'tools/lora_hw_tests.py'),'--out',str(out/'lora-hw')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/gm_input_order_tests.py'),'--deps',str(args.deps),'--out',str(out/'gm-input-order')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/cold_client_tests.py'),'--out',str(out/'cold-client')],env={**os.environ,'CC':compiler},check=True,timeout=90)
    subprocess.run([sys.executable,str(APP/'tools/gm_standby_tests.py'),'--deps',str(args.deps),'--out',str(out/'gm-standby')],env={**os.environ,'CC':compiler},check=True,timeout=360)
    subprocess.run([sys.executable,str(APP/'tools/lora_gm_tests.py'),'--deps',str(args.deps),'--out',str(out/'lora-gm')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/disconnect_diag_tests.py'),'--deps',str(args.deps),'--out',str(out/'disconnect-diag')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/ticker_fairness_tests.py'),'--deps',str(args.deps),'--out',str(out/'ticker-fairness')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/controller_fault_diag_tests.py'),'--deps',str(args.deps),'--out',str(out/'controller-fault-diag')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/watchdog_tests.py'),'--deps',str(args.deps),'--out',str(out/'watchdog')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/gnss_recovery_tests.py'),'--out',str(out/'gnss-recovery')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    # Two actual production harnesses share the observed terminal result. The
    # service publication bridge is source-checked, not a simulated worker run.
    service=(APP/'src/ble_service.c').read_text()
    assert 'if(r2_ble_stop_error())r2_service_stop();' in service
    assert 'status.error=r2_ble_stop_error();' in service
    subprocess.run([str(out/'gm-standby/transport'),'--terminal',str(out/'native-terminal.bin')],check=True,timeout=30)
    subprocess.run([str(out/'watchdog/watchdog-test'),str(out/'native-terminal.bin')],check=True,timeout=30)
    subprocess.run([sys.executable,str(APP/'tools/controller_radio_fix_tests.py'),'--deps',str(args.deps),'--out',str(out/'controller-radio-fixes')],env={**os.environ,'CC':compiler},check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/scan_aux_context_tests.py'),'--deps',str(args.deps),'--out',str(out/'scan-aux-context'),'--cc',compiler],check=True,timeout=120)
    subprocess.run([sys.executable,str(APP/'tools/controller_diag_tests.py'),'--deps',str(args.deps),'--out',str(out/'controller-diag')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/health_console_tests.py'),'--out',str(out/'health-console')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/health_register_tests.py'),'--deps',str(args.deps),'--out',str(out/'health-registers')],env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,'-m','unittest',str(APP/'tests/test_lora_build_binding.py')],check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/framebuffer_tests.py'),'--out',str(out/'framebuffer')],
                   env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/dc_tests.py'),'--out',str(out/'dc')],
                   env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/dc_gnss_tests.py'),'--out',str(out/'dc-gnss')],
                   env={**os.environ,'CC':compiler},check=True,timeout=60)
    subprocess.run([sys.executable,str(APP/'tools/pc_tests.py'),'--out',str(out/'pc')],
                   env={**os.environ,'CC':compiler},check=True,timeout=60)
    for suite in ['precision_schedule','initiator_aux','rc_renewal','rc_relay_admission','rc_relay_renewal','rc_source_wait','rc_transport_hold','usb_lifecycle','gm_discovery','rc_path_role','rc_gm_failover','rc_gm_admission','rc_mtu','rc_passive_scan']:
        subprocess.run([sys.executable,str(APP/'tools'/(suite+'_tests.py')),
                        '--deps',str(args.deps),'--out',str(out/suite)],
                       env={**os.environ,'CC':compiler},check=True,timeout=60)
    for suite in ['uc_cleanup','rc_advertising','rc_connection_lifecycle','rc_selection','rc_relay_discovery','rc_durability']:
        subprocess.run([sys.executable,str(APP/'tools'/(suite+'_tests.py')),
                        '--out',str(out/suite)],env={**os.environ,'CC':compiler},
                       check=True,timeout=60)
    flags=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(APP/'include'),'-I'+str(APP/'protocol')]
    subprocess.run([*flags,str(APP/'src/core.c'),str(APP/'tests/core_test.c'),'-o',str(out/'core')],check=True,timeout=30)
    subprocess.run([str(out/'core')],check=True,timeout=30)
    for name in ['kernel.h','device.h','drivers/uart.h','usb/usb_device.h','drivers/gpio.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','drivers/clock_control.h','drivers/clock_control/nrf_clock_control.h','sys/onoff.h','drivers/spi.h','drivers/sensor.h','drivers/pwm.h','drivers/flash.h']:
        p=out/'zephyr'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('/* Test boundary declarations are force-included. */\n')
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/console_test.c'),'-o',str(out/'console')],check=True,timeout=30)
    subprocess.run([str(out/'console')],check=True,timeout=30)

    subprocess.run([*flags,str(APP/'src/clock.c'),str(APP/'src/nmea.c'),str(APP/'tests/clock_test.c'),'-o',str(out/'clock')],check=True,timeout=30)
    subprocess.run([str(out/'clock')],check=True,timeout=30)
    subprocess.run([*flags,str(APP/'src/precision.c'),str(APP/'tests/precision_test.c'),'-o',str(out/'precision')],check=True,timeout=30)
    subprocess.run([str(out/'precision')],check=True,timeout=30)
    for name, sources in [('precision_codec',['protocol/precision_codec.c']),('radio_precision',['src/radio_precision.c'])]:
        subprocess.run([*flags,'-I'+str(APP/'protocol'),*[str(APP/p) for p in sources],str(APP/'tests'/f'{name}_test.c'),'-o',str(out/name)],check=True,timeout=30)
        subprocess.run([str(out/name)],check=True,timeout=30)
    for name in ['nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h']:
        (out/name).write_text('/* Public nrfx boundary supplied by capture_fake.h. */\n')
    (out/'hal').mkdir(exist_ok=True)
    (out/'hal/nrf_radio.h').write_text('/* Test radio boundary */\n')
    (out/'hal/nrf_timer.h').write_text('/* Test timer boundary */\n')
    (out/'zephyr/irq.h').write_text('/* Test IRQ boundary */\n')
    subprocess.run([*flags,'-I'+str(out),str(APP/'tests/radio_capture_test.c'),'-o',str(out/'radio_capture')],check=True,timeout=30)
    subprocess.run([str(out/'radio_capture')],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/capture_fake.h'),str(APP/'tests/capture_test.c'),'-o',str(out/'capture')],check=True,timeout=30)
    for mode in range(15):
        subprocess.run([str(out/'capture'),str(mode)],check=True,timeout=30)
    subprocess.run([*flags,'-Wno-unused-function','-Wno-unused-variable','-I'+str(out),'-I'+str(APP/'protocol'),str(APP/'tests/ble_transport_test.c'),'-Wno-default-const-init-var-unsafe','-I'+str(args.deps/'modules/crypto/tinycrypt/lib/include'),*[str(APP/'protocol'/n) for n in ['group_auth.c','group_control.c','authority_codec.c']],*[str(args.deps/'modules/crypto/tinycrypt/lib/source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'transport')],check=True,timeout=30)
    subprocess.run([str(out/'transport')],check=True,timeout=30)
    subprocess.run([sys.executable,str(APP/'tools/gm_drain_tests.py'),'--deps',str(args.deps),'--out',str(out/'gm-drain')],env={**os.environ,'CC':compiler},check=True,timeout=60)

    subprocess.run([*flags,'-I'+str(out),'-I'+str(APP/'protocol'),str(APP/'tests/radio_service_test.c'),'-Wno-default-const-init-var-unsafe','-I'+str(args.deps/'modules/crypto/tinycrypt/lib/include'),*[str(APP/'src'/n) for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']],*[str(APP/'protocol'/n) for n in ['gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']],*[str(args.deps/'modules/crypto/tinycrypt/lib/source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'radio_service')],check=True,timeout=30)
    # Current RC7 reset/reconfiguration cases; legacy direct-service fixture
    # modes 0..5 predate the independent bootstrap contract.
    for mode in (16,17,20,22,30,31,32,33,34,50):
        subprocess.run([str(out/'radio_service'),str(mode)],check=True,timeout=30)

    subprocess.run([*flags,'-std=gnu11','-I'+str(out),'-include',str(APP/'tests/gnss_fake.h'),str(APP/'tests/gnss_test.c'),'-o',str(out/'gnss')],check=True,timeout=30)
    for case in ['normal','failure','timeout','ui_wait','ui_fault']:
        subprocess.run([str(out/'gnss'),case],check=True,timeout=30)

    subprocess.run([*flags,'-Wno-unused-function','-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/platform_test.c'),'-o',str(out/'platform')],check=True,timeout=30)
    for mode in ['0','1','2','3']:
        subprocess.run([str(out/'platform'),mode],check=True,timeout=30)

    (out/'build_identity.h').write_text('#define R2_VERSION "0.1.0"\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#define R2_CHILD_COMMIT "'+'f'*40+'"\n#define R2_SOURCE_DIRTY 0\n')
    subprocess.run([*flags,'-I'+str(out),str(APP/'tests/timing_events_test.c'),str(APP/'src/core.c'),'-o',str(out/'timing_events')],check=True,timeout=30)
    subprocess.run([str(out/'timing_events')],check=True,timeout=30)
    subprocess.run([*flags,'-std=gnu11',str(APP/'tests/initiator_diag_test.c'),'-o',str(out/'initiator_diag')],check=True,timeout=30)
    subprocess.run([str(out/'initiator_diag')],check=True,timeout=30)
    # Only the native RC7 application is a supported public configuration.
    (out/'r2_enrollment.h').write_text('static const struct r2_enrollment r2_enrollment={0};\n')
    subprocess.run([*flags,'-DR2_NATIVE_BLE=1','-I'+str(APP/'protocol'),'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/reply_test.c'),str(APP/'src/core.c'),'-o',str(out/'reply-native')],check=True,timeout=30)
    subprocess.run([str(out/'reply-native')],check=True,timeout=30)


    subprocess.run([*flags,str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),str(APP/'tests/ui_test.c'),'-o',str(out/'ui')],check=True,timeout=30)
    subprocess.run([str(out/'ui'),str(out)],check=True,timeout=30)
    subprocess.run([*flags,str(APP/'src/precision.c'),str(APP/'tests/ui_precision_test.c'),'-o',str(out/'ui_precision')],check=True,timeout=30)
    subprocess.run([str(out/'ui_precision'),str(out)],check=True,timeout=30)
    subprocess.run([*flags,'-DR2_GM7_NATIVE=1',str(APP/'src/precision.c'),str(APP/'tests/ui_precision_test.c'),'-o',str(out/'ui_precision_gm7')],check=True,timeout=30)
    subprocess.run([str(out/'ui_precision_gm7'),str(out)],check=True,timeout=30)
    subprocess.run([*flags,str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),str(APP/'tests/ui_sync_display_test.c'),'-o',str(out/'ui_sync_display')],check=True,timeout=30)
    subprocess.run([str(out/'ui_sync_display')],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/display_fake.h'),str(APP/'tests/display_test.c'),'-o',str(out/'display')],check=True,timeout=30)
    subprocess.run([str(out/'display')],check=True,timeout=30)

    subprocess.run([*flags,'-c',str(APP/'src/precision.c'),'-o',str(out/'precision.o')],check=True,timeout=30)
    subprocess.run([*flags,'-c',str(APP/'src/ui_render.c'),'-o',str(out/'ui_render.o')],check=True,timeout=30)
    dynamic_ui=(APP/'src/rc_dynamic_service.c').read_text()
    begin=dynamic_ui.index('void r2_service_ui(struct r2_ui_model *m) {')
    (out/'rc_ui_binding_under_test.h').write_text(dynamic_ui[begin:dynamic_ui.index('\nint r2_service_relay(',begin)])
    subprocess.run([*flags,'-I'+str(out),str(APP/'tests/rc_ui_binding_test.c'),str(out/'ui_render.o'),str(out/'precision.o'),'-o',str(out/'rc_ui_binding')],check=True,timeout=30)
    subprocess.run([str(out/'rc_ui_binding')],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_service_test.c'),str(out/'ui_render.o'),str(out/'precision.o'),'-o',str(out/'ui_service')],check=True,timeout=30)
    for mode in map(str,[*range(34),*range(35,42),52,53]):
        subprocess.run([str(out/'ui_service'),mode],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_sync_worker_test.c'),str(out/'ui_render.o'),str(out/'precision.o'),'-o',str(out/'ui_sync_worker')],check=True,timeout=30)
    subprocess.run([str(out/'ui_sync_worker')],check=True,timeout=30)

    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/time_service_test.c'),str(APP/'src/clock.c'),str(APP/'src/nmea.c'),str(APP/'src/precision.c'),'-o',str(out/'time_service')],check=True,timeout=30)
    subprocess.run([str(out/'time_service')],check=True,timeout=30)

    subprocess.run([*flags,'-DR2_LORA=1','-I'+str(APP/'protocol'),'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/diagnostic_test.c'),str(APP/'src/core.c'),'-o',str(out/'diagnostic')],check=True,timeout=30)
    for mode in map(str,range(34)):
        subprocess.run([str(out/'diagnostic'),mode],check=True,timeout=30)
