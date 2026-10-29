#!/usr/bin/env python3
"""Inspect the linked ARM witness frame, argument forwarding and DWARF CFI.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.dwarf.callframe import FDE

def inspect(path,objdump):
 with Path(path).open('rb')as stream:
  elf=ELFFile(stream);assert elf['e_machine']=='EM_ARM'and elf.little_endian
  symbols={q.name:q for q in elf.get_section_by_name('.symtab').iter_symbols()if q.name}
  start=int(symbols['gm7_source_snapshot']['st_value'])&~1
  end=start+int(symbols['gm7_source_snapshot']['st_size'])
  inner=int(symbols['gm7_source_snapshot_inner']['st_value'])&~1
  fault=int(symbols['gm7_snapshot_witness_fault']['st_value'])&~1
  assert start<fault<end
  assert symbols['r2_crash_record']['st_size']==512 and symbols['r2_gm7_witness_state']['st_size']==1
  raw=subprocess.check_output([str(objdump),'-d','--start-address='+hex(start),'--stop-address='+hex(end),str(path)],text=True)
  rows=[]
  for line in raw.splitlines():
   q=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+){1,2}([a-z][a-z0-9.]*)\s*(.*)',line)
   if q:rows.append((int(q[1],16),q[2],q[3].split(';')[0].strip()))
  def has(mnemonic,operands):return any(m.startswith(mnemonic)and op==operands for _,m,op in rows)
  calls=[q for q in rows if q[1]=='bl'];assert len(calls)==1 and 'gm7_source_snapshot_inner'in calls[0][2]
  assert any(m.startswith('bne')and 'gm7_source_snapshot_inner'in op for _,m,op in rows)
  assert has('stmdb','sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}')
  assert has('sub','sp, #36')and has('add','sp, #52')
  assert has('ldmia','ip, {r0, r1, r2, r3}')
  call_index=rows.index(calls[0]);assert rows[call_index+1][1].startswith('str')and rows[call_index+1][2]=='r0, [sp, #32]'
  assert has('ldmia','sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}')
  for offset in range(0,32,4):
   source=f'r3, [sp, #{88+offset}]';target='r3, [sp'+(f', #{offset}'if offset else', #0')+']'
   # GNU objdump renders zero stack offsets explicitly.
   pair=next(i for i,q in enumerate(rows[:-1])if q[1].startswith('ldr')and q[2]==source and rows[i+1][1].startswith('str')and rows[i+1][2]==target)
   assert rows[pair+1][1].startswith('str')and rows[pair+1][2]==target and pair<call_index
  for offset in range(0,36,4):
   pair=next(i for i,q in enumerate(rows)if q[1].startswith('ldr')and q[2]==f'r0, [sp, #{52+offset}]')
   assert rows[pair+1][1].startswith('str')and rows[pair+1][2]==f'r0, [ip, #{84+offset}]'and pair>call_index
  assert has('stmia','r0, {r4, r5, r6, r7, r8, r9, sl, fp}')
  assert has('mrs','r0, PRIMASK')and has('cpsid','i')and has('msr','PRIMASK, r0')
  assert any(m=='udf'and op=='#188'and pc==fault for pc,m,op in rows)
  assert any(m.startswith('bne')and 'gm7_snapshot_witness_fault'in op and pc<fault for pc,m,op in rows)
  fdes=[q for q in elf.get_dwarf_info().CFI_entries()if isinstance(q,FDE)]
  fde=next(q for q in fdes if q['initial_location']==start)
  table=fde.get_decoded().table
  for pc in (calls[0][0],fault):
   cfi=next(q for q in reversed(table)if q['pc']<=pc)
   assert cfi['cfa'].reg==13 and cfi['cfa'].offset==88
   for reg in range(4,12):assert cfi[reg].type=='OFFSET'and cfi[reg].arg==-36+(reg-4)*4
   assert cfi[14].type=='OFFSET'and cfi[14].arg==-4
  chain=[]
  for caller,target in (('cycle','source_read'),('source_read','gm7_source_snapshot'),('gm7_source_snapshot_inner',None)):
   if caller not in symbols:continue # Freestanding QEMU has no service/cycle.
   base=int(symbols[caller]['st_value'])&~1;size=int(symbols[caller]['st_size'])
   cf=next(q for q in fdes if q['initial_location']==base)
   if target:
    code=subprocess.check_output([str(objdump),'-d','--start-address='+hex(base),'--stop-address='+hex(base+size),str(path)],text=True)
    calls_to=[q for q in code.splitlines()if re.search(r'\bbl\s',q)and '<'+target+'>'in q]
    assert len(calls_to)==1
    pc=int(calls_to[0].split(':',1)[0],16);row=next(q for q in reversed(cf.get_decoded().table)if q['pc']<=pc)
    assert row['cfa'].reg==13 and row['cfa'].offset%8==0
    chain.append(dict(caller=caller,target=target,call=hex(pc),frame_bytes_at_call=row['cfa'].offset))
   else:chain.append(dict(caller=caller,maximum_CFI_frame_bytes=max(q['cfa'].offset for q in cf.get_decoded().table)))
  return dict(status='PASS_NATIVE_ARM_WITNESS_ABI_CFI',elf_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
   wrapper_start=hex(start),inner_start=hex(inner),guard_fault=hex(fault),wrapper_code_bytes=end-start,
   static_witness_allocation_bytes=0,additional_frame_bytes=88,forwarded_stack_argument_bytes=32,
   unique_inner_calls=1,static_instructions=len(rows),copy_pairs=8,saved_slot_pairs=9,production_call_chain=chain,
   limits='Linked code/CFI and publicABI inspection only. QEMU execution is separate; no fullZephyr/IRQ concurrency, wall-time bound or CPUrepair proof.')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',required=True,type=Path);p.add_argument('--objdump',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
 assert not a.out.exists();a.out.write_text(json.dumps(inspect(a.elf,a.objdump),indent=2)+'\n');print('PASS_NATIVE_ARM_WITNESS_ABI_CFI')
