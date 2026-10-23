#!/usr/bin/env python3
"""Acquire pinned public MIT RadioLib for optional W12 LR2021 RX; no hardware."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dest',type=Path,required=True);a=p.parse_args()
if a.dest.exists():p.error('Fresh destination required')
r=json.loads((Path(__file__).resolve().parents[1]/'dependencies.lock.json').read_text())['radiolib']
subprocess.run(['git','clone','--branch',r['release'],'--depth','1',r['url'],str(a.dest)],check=True)
if subprocess.check_output(['git','-C',str(a.dest),'rev-parse','HEAD'],text=True).strip()!=r['commit']:raise ValueError('Public release identity changed')
