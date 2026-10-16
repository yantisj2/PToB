#!/usr/bin/env python3
"""Offline source/image/layout validation; not deployment authorization.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from uf2 import verify

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def inspect(out,allow_development=False):
    out=Path(out);r=json.loads((out/'receipt.json').read_text())
    require(r['artifact_class']=='PTOB_T096_RC7' and r['profile'] in ('GM','RC') and r['usb_source_stream'] is True,'Unknown profile')
    require(r['status']=='BUILD_COMPLETE_PHYSICAL_UNKNOWN' or allow_development and r['status']=='DEVELOPMENT_NOT_FLASHABLE','Unreviewable build')
    require(not r['source_dirty'] or allow_development,'Dirty source')
    for name,h in r['artifacts'].items():require(sha(out/name)==h,'Changed artifact: '+name)
    for name,h in r['sources'].items():require(sha(out/'source'/name)==h,'Changed source snapshot: '+name)
    require(hashlib.sha256(json.dumps(r['sources'],sort_keys=True,separators=(',',':')).encode()).hexdigest()==r['source_sha256'],'Source binding')
    image=out/'t096.uf2';verify(image,0x26000)
    data=image.read_bytes();require(0x26000+len(data)//512*256<=0xdf000,'UF2 enters credential reservation')
    payload=b''.join(data[i+32:i+288] for i in range(0,len(data),512))
    binary=(out/'target/zephyr/zephyr.bin').read_bytes()
    require(payload[:len(binary)]==binary and all(x==0 for x in payload[len(binary):]),'UF2/BIN mismatch')
    flash_end=0;ram_end=0
    with (out/'target/zephyr/zephyr.elf').open('rb') as f:
        elf=ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type']!='PT_LOAD':continue
            if seg['p_filesz']:
                require(0x26000<=seg['p_paddr'] and seg['p_paddr']+seg['p_filesz']<=0xdf000,'ELF load bounds')
                flash_end=max(flash_end,seg['p_paddr']+seg['p_filesz'])
            if seg['p_vaddr']>=0x20000000 and seg['p_memsz']:
                require(0x20002000<=seg['p_vaddr'] and seg['p_vaddr']+seg['p_memsz']<=0x20040000,'ELF RAM bounds')
                ram_end=max(ram_end,seg['p_vaddr']+seg['p_memsz'])
    config=(out/'target/zephyr/.config').read_text()
    for line in ['CONFIG_ROM_END_OFFSET=0x6000','CONFIG_FLASH=y','CONFIG_USB_DEVICE_STACK=y','CONFIG_WATCHDOG=y']:
        require(line in config,'Required configuration: '+line)
    commands=(out/'target/compile_commands.json').read_text()
    for feature in ['R2_RC_PROFILE=1','R2_USB_SOURCE_STREAM=1','R2_USB_SOF_PROBE=1']:
        require(feature in commands,'Missing standard RC7 feature: '+feature)
    binding=hashlib.sha256((r['source_sha256']+':'+sha(out/'r2_enrollment.h')+':'+sha(out/'target/zephyr/.config')).encode()).hexdigest()
    require(binding==r['watchdog_binding_sha256'],'Watchdog binding')
    return {'status':'PASS','flash_load_end':flash_end,'ram_allocated_end':ram_end,'static_unallocated_bytes':0x20040000-ram_end,'hardware':'NOT_TESTED','deployment_authorized':False}
