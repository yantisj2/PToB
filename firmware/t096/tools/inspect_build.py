#!/usr/bin/env python3
"""Offline source/image/layout validation; not deployment authorization.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,contextlib,hashlib,json,re,struct,sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
from uf2 import verify

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def inspect(out,allow_development=False):
    out=Path(out);r=json.loads((out/'receipt.json').read_text())
    dc=r['profile']=='DC'
    require((r['artifact_class']=='PTOB_T096_DC1' if dc else r['artifact_class']=='PTOB_T096_RC7' and r['profile'] in ('GM','RC')) and r['usb_source_stream'] is True,'Unknown profile')
    require(r['status']=='BUILD_COMPLETE_PHYSICAL_UNKNOWN' or allow_development and r['status']=='DEVELOPMENT_NOT_FLASHABLE','Unreviewable build')
    require(not r['source_dirty'] or allow_development,'Dirty source')
    for name,h in r['artifacts'].items():require(sha(out/name)==h,'Changed artifact: '+name)
    for name,h in r['sources'].items():require(sha(out/'source'/name)==h,'Changed source snapshot: '+name)
    version=re.search(r'^project\(ptob_t096 VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)',
                      (out/'source/CMakeLists.txt').read_text(),re.M)
    require(version is not None and version[1]==r['version'],'Receipt/application version mismatch')
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
    if not dc and tuple(map(int,r['version'].split('.'))) >= (0,15,63):
        rx=re.search(r'^CONFIG_BT_CTLR_RX_BUFFERS=(\d+)$',config,re.M)
        work=re.search(r'^CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=(\d+)$',config,re.M)
        require(rx is not None and int(rx[1])>=8,'Insufficient native RX reserve')
        require(work is not None and int(work[1])>=5120,'Insufficient workqueue stack')
        require(0x20040000-ram_end>=1024,'Insufficient static RAM margin')
    for line in ['CONFIG_ROM_END_OFFSET=0x6000','CONFIG_FLASH=y','CONFIG_USB_DEVICE_STACK=y','CONFIG_WATCHDOG=y']:
        require(line in config,'Required configuration: '+line)
    commands=(out/'target/compile_commands.json').read_text()
    if not dc and tuple(map(int,r['version'].split('.'))) >= (0,15,101):
        units=json.loads(commands)
        aux=[x['file'] for x in units if x['file'].endswith('/lll_scan_aux.c')]
        require(len(aux)==1 and aux[0].endswith('/r2_radio_controller/lll_scan_aux.c'),
                'Missing/duplicate pinned AUX initiation diagnostic source')
    dts=(out/'target/zephyr/zephyr.dts').read_text()
    if dc and 'src/dc_gnss.c' in r['sources']:
        require(r.get('dc_gnss_bridge') is True,'Missing DC GNSS bridge receipt')
        require('/src/dc_gnss.c' in commands and '/src/dc_gnss_transport.c' in commands and 'cdc_acm_gnss' in dts,'Missing DC GNSS serial function')
    else:
        require('/src/dc_gnss.c' not in commands and '/src/dc_gnss_transport.c' not in commands and 'cdc_acm_gnss' not in dts,'DC GNSS bridge leaked into GM/RC')
    for feature in [('R2_DC_PROFILE=1' if dc else 'R2_RC_PROFILE=1'),'R2_USB_SOURCE_STREAM=1','R2_USB_SOF_PROBE=1']:
        require(feature in commands,'Missing standard RC7 feature: '+feature)
    if dc:
        require('# CONFIG_BT is not set' in config and not re.search(r'^CONFIG_BT\w*=y$',config,re.M),'DC Bluetooth enabled')
        require(r.get('enrollment_mode')=='NONE_DC_LOCAL_GNSS' and r.get('ble_enabled') is False and
                r.get('profile_toggleable') is False and r.get('relay_hops')==0,'DC profile contract')
        require(not any(x in commands for x in ['R2_RC_PROFILE=1','R2_NATIVE_BLE=1','R2_SWITCHABLE_FIELD=1',
                '/src/ble_service.c','/src/rc_dynamic_service.c','/src/radio_capture.c',
                '/protocol/group_auth.c','/subsys/bluetooth/']),'DC contains mesh/controller owners')
        require('psk' not in (out/'r2_enrollment.h').read_text().lower(),'DC enrollment contains a group key')
        with (out/'target/zephyr/zephyr.elf').open('rb') as f:
            symbols={s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        require(not any(x in symbols for x in ['bt_enable','bt_le_scan_start','bt_le_adv_start',
                'r2_service_profile','r2_radio_capture_enable','r2_uc_start']),'DC radio/switch symbols')
        require('r2_dc_poll' in symbols and 'r2_usb_source_request' in symbols,'Missing DC publisher/USB interface')
        usb_stack=re.search(r'^CONFIG_USB_NRFX_WORK_QUEUE_STACK_SIZE=(\d+)$',config,re.M)
        require(usb_stack is not None and int(usb_stack[1])>=2048,'Insufficient DC USB worker stack')
        require(0x20040000-ram_end>=1024,'Insufficient DC static RAM margin')
    binding=hashlib.sha256((r['source_sha256']+':'+sha(out/'r2_enrollment.h')+':'+sha(out/'target/zephyr/.config')).encode()).hexdigest()
    require(binding==r['watchdog_binding_sha256'],'Watchdog binding')
    return {'status':'PASS','flash_load_end':flash_end,'ram_allocated_end':ram_end,'static_unallocated_bytes':0x20040000-ram_end,'hardware':'NOT_TESTED','deployment_authorized':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out',type=Path,help='Existing build output directory')
    args=parser.parse_args()
    with contextlib.redirect_stdout(sys.stderr):
        result=inspect(args.out)
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    main()
