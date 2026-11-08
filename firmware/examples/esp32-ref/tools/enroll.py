#!/usr/bin/env python3
"""Create fresh local enrollment for an isolated ESP32-REF experiment.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,secrets
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args()
    if a.out.exists():p.error('enrollment exists; keep using it for the same experiment')
    os.umask(0o077);a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('x') as stream:json.dump({'psk':secrets.token_hex(32)},stream);stream.write('\n')
    print('Created owner-only enrollment:',a.out)
    print('Use the same local file for every participant. The key was not printed.')
if __name__=='__main__':main()
