#!/usr/bin/env python3
"""Focused actual C core/CDC adapter regressions, using ASan and UBSan; no hardware."""
from pathlib import Path
import os
import argparse
p=argparse.ArgumentParser();p.add_argument("--deps",type=Path,required=True);args=p.parse_args()
import shutil
import subprocess
import tempfile
APP=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='t096-r2-test-') as directory:
    out=Path(directory)
    compiler=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang'))
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

    subprocess.run([*flags,'-I'+str(out),'-I'+str(APP/'protocol'),str(APP/'tests/radio_service_test.c'),'-Wno-default-const-init-var-unsafe','-I'+str(args.deps/'modules/crypto/tinycrypt/lib/include'),*[str(APP/'src'/n) for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c']],*[str(APP/'protocol'/n) for n in ['role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']],*[str(args.deps/'modules/crypto/tinycrypt/lib/source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'radio_service')],check=True,timeout=30)
    # Current RC7 reset/reconfiguration cases; legacy direct-service fixture
    # modes 0..5 predate the independent bootstrap contract.
    for mode in (16,17,20):
        subprocess.run([str(out/'radio_service'),str(mode)],check=True,timeout=30)

    subprocess.run([*flags,'-std=gnu11','-I'+str(out),'-include',str(APP/'tests/gnss_fake.h'),str(APP/'tests/gnss_test.c'),'-o',str(out/'gnss')],check=True,timeout=30)
    for case in ['normal','failure','timeout','ui_wait','ui_fault']:
        subprocess.run([str(out/'gnss'),case],check=True,timeout=30)

    subprocess.run([*flags,'-Wno-unused-function','-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/platform_test.c'),'-o',str(out/'platform')],check=True,timeout=30)
    for mode in ['0','1','2','3']:
        subprocess.run([str(out/'platform'),mode],check=True,timeout=30)

    (out/'build_identity.h').write_text('#define R2_VERSION "0.1.0"\n#define R2_SOURCE_SHA "'+'f'*64+'"\n#define R2_CHILD_COMMIT "'+'f'*40+'"\n#define R2_SOURCE_DIRTY 0\n')
    subprocess.run([*flags,'-I'+str(out),str(APP/'tests/timing_events_test.c'),'-o',str(out/'timing_events')],check=True,timeout=30)
    subprocess.run([str(out/'timing_events')],check=True,timeout=30)
    subprocess.run([*flags,'-std=gnu11',str(APP/'tests/initiator_diag_test.c'),'-o',str(out/'initiator_diag')],check=True,timeout=30)
    subprocess.run([str(out/'initiator_diag')],check=True,timeout=30)
    # Only the native RC7 application is a supported public configuration.
    (out/'r2_enrollment.h').write_text('static const struct r2_enrollment r2_enrollment={0};\n')
    subprocess.run([*flags,'-DR2_NATIVE_BLE=1','-I'+str(APP/'protocol'),'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/reply_test.c'),str(APP/'src/core.c'),'-o',str(out/'reply-native')],check=True,timeout=30)
    subprocess.run([str(out/'reply-native')],check=True,timeout=30)


    subprocess.run([*flags,str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),str(APP/'tests/ui_test.c'),'-o',str(out/'ui')],check=True,timeout=30)
    subprocess.run([str(out/'ui'),str(out)],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/display_fake.h'),str(APP/'tests/display_test.c'),'-o',str(out/'display')],check=True,timeout=30)
    subprocess.run([str(out/'display')],check=True,timeout=30)

    subprocess.run([*flags,'-c',str(APP/'src/precision.c'),'-o',str(out/'precision.o')],check=True,timeout=30)
    subprocess.run([*flags,'-c',str(APP/'src/ui_render.c'),'-o',str(out/'ui_render.o')],check=True,timeout=30)
    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_service_test.c'),str(out/'ui_render.o'),str(out/'precision.o'),'-o',str(out/'ui_service')],check=True,timeout=30)
    for mode in ['0','1','2','3','4','5','6','7']:
        subprocess.run([str(out/'ui_service'),mode],check=True,timeout=30)

    subprocess.run([*flags,'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/time_service_test.c'),str(APP/'src/clock.c'),str(APP/'src/nmea.c'),str(APP/'src/precision.c'),'-o',str(out/'time_service')],check=True,timeout=30)
    subprocess.run([str(out/'time_service')],check=True,timeout=30)

    subprocess.run([*flags,'-I'+str(APP/'protocol'),'-I'+str(out),'-include',str(APP/'tests/zephyr_fake.h'),str(APP/'tests/diagnostic_test.c'),str(APP/'src/core.c'),'-o',str(out/'diagnostic')],check=True,timeout=30)
    for mode in map(str,range(20)):
        subprocess.run([str(out/'diagnostic'),mode],check=True,timeout=30)
