#!/usr/bin/env python3
"""Actual PT publication path with synthetic source and kernel/radio boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 for name in ['kernel.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
  f=a.out/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Existing radio_service_fake boundary. */\n')
 crypto=a.deps/'modules/crypto/tinycrypt/lib';flags=[os.environ.get('CC','clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-DR2_LORA=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(a.out.resolve()),'-I'+str(crypto/'include')]
 sources=[APP/'tests/radio_service_test.c',*[APP/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c','lora_source.c','lora_rc8_policy.c']],*[APP/'protocol'/n for n in ['gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c','lora_rc8.c']],*[crypto/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
 binary=a.out/'source_owner';subprocess.run([*flags,*map(str,sources),'-o',str(binary)],check=True)
 for mode in range(23,30):subprocess.run([str(binary),str(mode)],check=True)
if __name__=='__main__':main()
