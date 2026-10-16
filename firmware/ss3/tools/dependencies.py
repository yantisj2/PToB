#!/usr/bin/env python3
"""Acquire the pinned public ESP-IDF with recursive submodules; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dest',type=Path,required=True);a=p.parse_args()
 if a.dest.exists():p.error('Use a fresh destination')
 lock=json.loads((APP/'dependencies.lock.json').read_text())['esp_idf']
 subprocess.run(['git','clone','--branch',lock['release'],'--depth','1','--recurse-submodules','--shallow-submodules',lock['url'],str(a.dest)],check=True)
 if subprocess.check_output(['git','-C',str(a.dest),'rev-parse','HEAD'],text=True).strip()!=lock['commit']:raise ValueError('SDK tag changed')
 print('Pinned SDK acquired. Run its install.sh esp32s3 and source export.sh before building.')
if __name__=='__main__':main()
