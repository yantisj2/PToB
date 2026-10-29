#!/usr/bin/env python3
"""Actual Cortex-M4 source snapshot/ABI canary in local QEMU; offline only.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
from snapshot_witness_inspect import inspect
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--toolchain',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
p.add_argument('--deps',required=True,type=Path)
p.add_argument('--baseline-app',required=True,type=Path)
p.add_argument('--qemu',default=shutil.which('qemu-system-arm'))
a=p.parse_args();app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
cc=a.toolchain/'bin/arm-zephyr-eabi-gcc';objdump=a.toolchain/'bin/arm-zephyr-eabi-objdump'
assert cc.is_file() and a.qemu
(out/'link.ld').write_text('ENTRY(_start)\nSECTIONS { . = 0; .vectors : {KEEP(*(.vectors))} .text : {*(.text*) *(.rodata*)} . = 0x20000000; .data : {*(.data*)} .bss (NOLOAD) : {__bss_start = .; *(.bss*) *(COMMON) __bss_end = .;} }\n')
rows=[]
try:
 for name,case,body in [('baseline187',0,a.baseline_app),('production188',0,app),*[(f'negative-{n}',n,app)for n in range(1,6)]]:
  elf=out/(name+'.elf');log=out/(name+'.log')
  command=[str(cc),'-mcpu=cortex-m4','-mthumb','-mabi=aapcs','-mtp=soft','-Os','-g','-gdwarf-4','-fno-strict-aliasing','-fno-common','-ffunction-sections','-fdata-sections','-fno-builtin','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-imacros',str(a.deps/'zephyr/include/zephyr/toolchain/zephyr_stdint.h'),'-I'+str(a.deps/'modules/crypto/tinycrypt/lib/include'),'-DCONFIG_ARM=1','-DR2_GM7_NATIVE=1',f'-DTEST_CASE={case}',*(['-DTEST_BASELINE=1']if name=='baseline187'else[]),'-I'+str(body/'include'),'-I'+str(app/'include'),'-I'+str(body/'protocol'),str(app/'tests/snapshot_arm_start.S'),str(app/'tests/snapshot_arm_test.c')]
  if case==0 or case==5:command += [str(body/'src/gm7_source.c'),str(body/'src/precision.c'),str(body/'protocol/gm7_codec.c')]
  if name!='baseline187':command += [str(app/'src/gm7_snapshot_witness.S')]
  command += ['-nostdlib','-Wl,--gc-sections','-T',str(out/'link.ld'),'-lgcc','-o',str(elf)]
  compiled=subprocess.run(command,capture_output=True,timeout=45)
  (out/(name+'.compile.log')).write_bytes(compiled.stdout+compiled.stderr)
  assert compiled.returncode==0,(out/(name+'.compile.log')).read_text()
  (out/(name+'.disassembly')).write_bytes(subprocess.check_output([str(objdump),'-d',str(elf)]))
  run=[a.qemu,'-M','mps2-an386','-cpu','cortex-m4','-nographic','-monitor','none','-serial','none','-semihosting-config','enable=on,target=native','-kernel',str(elf)]
  q=subprocess.run(run,capture_output=True,timeout=45);log.write_bytes(q.stdout+q.stderr)
  rows.append(dict(name=name,command=command,run_command=run,returncode=q.returncode,log=log.name,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest()))
  assert q.returncode==0 and b'PASS ARM' in log.read_bytes(),name+': '+log.read_text()
  if name=='production188':
   proof=inspect(elf,objdump);(out/'abi-cfi.json').write_text(json.dumps(proof,indent=2)+'\n');rows[-1]['native_abi_cfi']=proof
 print('PASS actual Cortex-M4 production187/188 snapshot20000 cases each and5 real veneer negatives')
finally:
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS'if len(rows)==7 and all(q['returncode']==0 for q in rows)else'INCOMPLETE',hardware_operated=False,rows=rows,limits='QEMU Cortex-M4 pure call path and synthetic offline boundary violations; no fullZephyr/controller/IRQ concurrency or CPUrepair proof.'),indent=2)+'\n')
