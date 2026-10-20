#!/usr/bin/env python3
"""Full SX1262 owner with fake peripheral/capture/DMA boundaries, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 a.out.mkdir(parents=True,exist_ok=False)
 source=(APP/'src/lora_hw.c').read_text()
 body=a.out/'owner_body.c'
 body.write_text(re.sub(r'^#include[^\n]*\n','',source,flags=re.M))
 binary=a.out/'lora_hw_test'
 command=[os.environ.get('CC','clang'),'-std=c11','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(APP/'include'),'-DR2_LORA_OWNER_BODY="'+str(body.resolve())+'"',str(APP/'tests/lora_hw_test.c'),'-o',str(binary)]
 subprocess.run(command,check=True)
 for mode in range(1,51):subprocess.run([str(binary),str(mode)],check=True)
 for name in ['nrfx_timer.h','nrfx_ppi.h','nrfx_gpiote.h','radio_nrf5_resources.h','hal/nrf_radio.h','hal/nrf_usbd.h','zephyr/drivers/clock_control/nrf_clock_control.h','zephyr/drivers/gpio.h','zephyr/sys/atomic.h','zephyr/sys/onoff.h']:
  h=a.out/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Driver boundary supplied by existing capture fake. */\n')
 capture=a.out/'lora_capture_test'
 flags=[os.environ.get('CC','clang'),'-std=c11','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(APP/'include'),'-I'+str(a.out.resolve())]
 subprocess.run([*flags,'-include',str(APP/'tests/usb_capture_fake.h'),str(APP/'tests/lora_capture_test.c'),'-o',str(capture)],check=True)
 for mode in range(1,12):subprocess.run([str(capture),str(mode)],check=True)
 files=['src/lora_hw.c','include/lora_hw.h','src/capture.c','include/capture.h','include/precision.h','tests/capture_fake.h','tests/usb_capture_fake.h','tests/lora_hw_test.c','tests/lora_capture_test.c','tools/lora_hw_tests.py']
 files.append('include/lora_watchdog.h')
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FAKE_BOUNDARY_ONLY',owner_modes=50,capture_modes=11,physical_qualification=False,sources={f:hashlib.sha256((APP/f).read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
 print('PASS50 production LoRa owner +11 shared-capture fake-boundary cases')
if __name__=='__main__':main()
