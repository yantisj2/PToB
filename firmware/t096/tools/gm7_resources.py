#!/usr/bin/env python3
"""Offline compiler-frame/call evidence; never runtime stack qualification.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--deps',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();target=a.build.resolve()/'target';app=Path(__file__).resolve().parents[1]
frames={};frame_files={}
for f in sorted(target.rglob('*.su')):
 for row in f.read_text().splitlines():
  parts=row.split('\t')
  if len(parts)!=3:continue
  name=parts[0].rsplit(':',1)[-1]
  frames[name]=max(frames.get(name,0),int(parts[1]));frame_files[name]=str(f.relative_to(target))
objdump=a.deps.resolve()/'zephyr-sdk-0.17.0/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump'
elf=target/'zephyr/zephyr.elf'
text=subprocess.check_output([str(objdump),'-d',str(elf)],text=True)
calls={};tails={};indirect={};names={};by_name={};current=None
for row in text.splitlines():
 m=re.match(r'^([0-9a-f]+) <([^>]+)>:$',row)
 if m:
  current=int(m[1],16);names[current]=m[2];by_name.setdefault(m[2],[]).append(current)
  calls.setdefault(current,set());tails.setdefault(current,set());continue
 if current is None:continue
 m=re.search(r'\s(bl|blx|b\.w)\s+([0-9a-f]+) <([^>]+)>',row)
 if m and '+' not in m[3] and int(m[2],16)!=current:
  (tails if m[1]=='b.w' else calls)[current].add(int(m[2],16))
 elif re.search(r'\sblx\s+r\d+',row):indirect.setdefault(names[current],[]).append(row.strip())
# These two callback pointers are bound once by the real serialized GM owner.
for caller,callee in [('clock_now','hf_read'),('gm7_owner_reservation_cancel','reservation_fence')]:
 for x in by_name.get(caller,[]):calls[x].update(by_name.get(callee,[]))
unknown=set();cycles=set()
def size(address):
 name=names.get(address,hex(address))
 if name not in frames:unknown.add(name)
 return frames.get(name,0)
def longest(address,seen=()):
 name=names.get(address,hex(address))
 if address in seen:cycles.add(name);return (0,[])
 base=size(address);best=(base,[name])
 for child in calls.get(address,[]):
  n,path=longest(child,seen+(address,));option=(base+n,[name]+path)
  if option[0]>best[0]:best=option
 for child in tails.get(address,[]):
  n,path=longest(child,seen+(address,));option=(max(base,n),[name+' (tail)']+path)
  if option[0]>best[0]:best=option
 return best
roots={}
for root in ['cycle','main','prepare_radio','auxiliary_isr','finish_isr','gm7_ticker_job']:
 n,path=max((longest(address)for address in by_name.get(root,[])),default=(0,[]))
 roots[root]={'known_direct_chain_bytes':n,'chain':path}
config=(target/'zephyr/.config').read_text();wanted=['MAIN_STACK_SIZE','ISR_STACK_SIZE','SYSTEM_WORKQUEUE_STACK_SIZE','HEAP_MEM_POOL_SIZE','BT_MAX_CONN','BT_EXT_ADV_MAX_ADV_SET','BT_PER_ADV_SYNC_MAX','BT_CTLR_RX_BUFFERS','BT_BUF_ACL_TX_COUNT','BT_BUF_EVT_RX_COUNT','BT_BUF_CMD_TX_COUNT']
settings={k:int(re.search(r'^CONFIG_'+k+r'=(\d+)$',config,re.M)[1]) for k in wanted}
new=[*app.glob('src/gm7*.c'),*app.glob('protocol/gm7*.c')]
alloc=[str(f.relative_to(app)) for f in new if re.search(r'\b(?:malloc|calloc|realloc|k_malloc|k_calloc)\s*\(',f.read_text())]
result={'class':'OFFLINE_STATIC_EVIDENCE_ONLY','elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'config_sha256':hashlib.sha256(config.encode()).hexdigest(),'roots':roots,'config':settings,'new_dynamic_allocations':alloc,'unknown_frame_functions':sorted(unknown),'call_cycles':sorted(cycles),'indirect_sites':indirect,'frames':frames,'frame_files':frame_files,'limits':'Compiler .su direct-call graph excludes runtime high-water, interrupt nesting, unresolved indirect callbacks and assembly/library frames without .su. Tail calls replace frames. Manual allowances and exact execution owners remain review obligations; linker headroom is separate.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'roots':roots,'new_dynamic_allocations':alloc,'config':settings},indent=2))
