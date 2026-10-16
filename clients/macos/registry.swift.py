#!/usr/bin/env python3
"""Generate Swift display aliases with the shared registry validator.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'registry'))
from registry import load
p=argparse.ArgumentParser(description=__doc__);p.add_argument('registry',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
data=load(a.registry)
lines=['// Generated display aliases; never membership or timing authority.','let displayRegistrySHA256 = '+json.dumps(hashlib.sha256(a.registry.read_bytes()).hexdigest()),'let displayRegistry: [String:String] = [']
lines += ['    '+json.dumps(k)+': '+json.dumps(v['alias'])+',' for k,v in sorted(data['devices'].items())]
lines += [']','func displayName(_ identity: String?) -> String {','    guard let id = identity, id != "0000000000000000" else { return "UNKNOWN" }','    return displayRegistry[id.lowercased()] ?? id','}']
a.output.write_text('\n'.join(lines)+'\n')
