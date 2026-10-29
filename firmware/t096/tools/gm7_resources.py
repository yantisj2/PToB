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
calls={};tails={};indirect={};names={};by_name={};bodies={};current=None
for row in text.splitlines():
 m=re.match(r'^([0-9a-f]+) <([^>]+)>:$',row)
 if m:
  current=int(m[1],16);names[current]=m[2];by_name.setdefault(m[2],[]).append(current)
  calls.setdefault(current,set());tails.setdefault(current,set());bodies.setdefault(current,[]);continue
 if current is None:continue
 bodies[current].append(row)
 m=re.search(r'\s(bl|blx|b\.w)\s+([0-9a-f]+) <([^>]+)>',row)
 if m and '+' not in m[3] and int(m[2],16)!=current:
  (tails if m[1]=='b.w' else calls)[current].add(int(m[2],16))
 elif re.search(r'\sblx\s+r\d+',row):indirect.setdefault(names[current],[]).append(row.strip())
# These two callback pointers are bound once by the real serialized GM owner.
for caller,callee in [('clock_now','hf_read'),('gm7_owner_reservation_cancel','reservation_fence')]:
 for x in by_name.get(caller,[]):calls[x].update(by_name.get(callee,[]))
# Linked snprintf installs the concrete leaf callback, not an arbitrary user
# FILE. Verify its Thumb pointer literal before resolving this indirect edge.
format_target=by_name.get('__file_str_put',[])
format_evidence={}
if format_target:
 assert len(format_target)==1 and len(by_name.get('snprintf',[]))==1
 body='\n'.join(bodies[by_name['snprintf'][0]])
 assert re.search(r'\.word\s+0x0*'+format(format_target[0]|1,'x')+r'\b',body)
 format_callers=[names[x] for x,edges in calls.items() if by_name['__l_vfprintf'][0] in edges]
 assert set(format_callers)=={'snprintf'},format_callers
 for x in by_name['__l_vfprintf']:calls[x].update(format_target)
 format_evidence={'caller':'snprintf','format':'__l_vfprintf','callback':'__file_str_put','callback_address':hex(format_target[0]),'binding':'linked Thumb literal in snprintf FILE initializer; leaf disassembly included in frame audit'}
unknown=set();cycles=set();assembly={};dynamic_stack=[]
def size(address):
 name=names.get(address,hex(address));canonical=re.sub(r'\.(isra|constprop)\.\d+',r'.\1',name)
 if name in frames:return frames[name]
 if canonical in frames:return frames[canonical]
 unknown.add(name);total=0
 for row in bodies.get(address,[]):
  m=re.search(r'\b(push(?:\.w)?|vpush|stmdb(?:\.w)?|vstmdb(?:\.w)?)\s+(?:sp!,\s*)?\{([^}]+)\}',row)
  if m:
   for part in m[2].split(','):
    part=part.strip();r=re.fullmatch(r'([rsd])(\d+)(?:-([rsd])(\d+))?',part)
    count=int(r[4])-int(r[2])+1 if r and r[4] else 1
    total+=count*(8 if part.startswith('d') else 4)
  m=re.search(r'\bsub(?:\.w)?\s+sp,\s*(?:sp,\s*)?#(0x[0-9a-f]+|[0-9]+)',row)
  if m:total+=int(m[1],0)
  if re.search(r'\bsub(?:\.w)?\s+sp,\s*sp,\s*r',row):dynamic_stack.append(name)
 assembly[name]=total
 return total
# The only GM7 cancellation cycle is state-bounded: exposed failure clears
# reserved before withdraw/disarm; unexposed cancellation either clears it or
# calls that exposed path. Conservatively allow each SCC function twice.
# This is a finite path over-approximation, not a general recursion proof.
bounded_cancel={'disarm','gm7_owner_reservation_cancel','gm7_owner_reservation_failed','gm7_owner_withdraw'}
def longest(address,seen=(),normal=False):
 name=names.get(address,hex(address))
 if address in seen and (name not in bounded_cancel or seen.count(address)>=2):cycles.add(name);return (0,[])
 base=size(address);best=(base,[name])
 for child in calls.get(address,[]):
  if normal and names.get(child)=='shadow_field_evaluate' and any(names.get(x)=='r2_shadow_poll_normal' for x in seen):continue
  n,path=longest(child,seen+(address,),normal);option=(base+n,[name]+path)
  if option[0]>best[0]:best=option
 for child in tails.get(address,[]):
  n,path=longest(child,seen+(address,),normal);option=(max(base,n),[name+' (tail)']+path)
  if option[0]>best[0]:best=option
 return best
roots={}
for root in ['cycle','main','prepare_radio','auxiliary_isr','finish_isr','gm7_ticker_job','owner','precision_owner','supervisor_owner','gatt_dispatch','tx_processor','work_queue_main','recv_thread','prio_recv_thread','rx_work_handler','prepare_lll','r2_gm7_precision_anchor','hci_driver_send','gm7_owner_reservation_cancel','gm7_owner_reservation_failed','gm7_owner_withdraw','disarm']:
 n,path=max((longest(address)for address in by_name.get(root,[])),default=(0,[]))
 roots[root]={'known_direct_chain_bytes':n,'chain':path}
# Source-constrained normal main excludes only the explicitly false FIELD path.
shadow=(app/'src/rc_shadow.c').read_text()
assert 'void r2_shadow_poll_normal(void){shadow_poll(false);}' in shadow and 'if(allow_field)shadow_field_evaluate(&r)' in shadow
n,path=max((longest(x,normal=True)for x in by_name['main']))
roots['main_normal_context']={'known_direct_chain_bytes':n,'chain':path}
wrappers={'workqueue_local_frame':frames.get('work_queue_main',0),'thread_entry_local_frame':frames.get('z_thread_entry',0)}
config=(target/'zephyr/.config').read_text();wanted=['MAIN_STACK_SIZE','ISR_STACK_SIZE','SYSTEM_WORKQUEUE_STACK_SIZE','HEAP_MEM_POOL_SIZE','BT_MAX_CONN','BT_EXT_ADV_MAX_ADV_SET','BT_PER_ADV_SYNC_MAX','BT_CTLR_RX_BUFFERS','BT_BUF_ACL_TX_COUNT','BT_BUF_EVT_RX_COUNT','BT_BUF_CMD_TX_COUNT']
settings={k:int(re.search(r'^CONFIG_'+k+r'=(\d+)$',config,re.M)[1]) for k in wanted}
new=[*app.glob('src/gm7*.c'),*app.glob('protocol/gm7*.c')]
alloc=[str(f.relative_to(app)) for f in new if re.search(r'\b(?:malloc|calloc|realloc|k_malloc|k_calloc)\s*\(',f.read_text())]
result={'class':'OFFLINE_STATIC_EVIDENCE_ONLY','elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'config_sha256':hashlib.sha256(config.encode()).hexdigest(),'roots':roots,'config':settings,'new_dynamic_allocations':alloc,'unknown_frame_functions':sorted(unknown),'call_cycles':sorted(cycles),'indirect_sites':indirect,'wrapper_frames':wrappers,'format_callback':format_evidence,'bounded_cancellation_component':sorted(bounded_cancel),'assembly_static_allocation_sum':assembly,'assembly_dynamic_stack':sorted(set(dynamic_stack)),'frames':frames,'frame_files':frame_files,'limits':'Compiler .su/direct-call graph with separate addresses for duplicate static names. Missing .su falls back to an auditable sum of static push/sub-SP instructions; this is not a CFG/loop proof. Unresolved indirect callbacks, tail/recursive cycles, exception/nesting and wrapper execution context need manual review. main_normal_context excludes only explicit shadow_poll(false) FIELD path. Linker and arithmetic remaining stack space are unaccounted headroom, never runtime high-water or measured reserve.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'roots':roots,'new_dynamic_allocations':alloc,'config':settings},indent=2))
