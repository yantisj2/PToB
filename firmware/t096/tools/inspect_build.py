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
    pc=r['profile']=='PC'
    require((r['artifact_class']=='PTOB_T096_DC1' if dc else r['artifact_class']=='PTOB_T096_PC1' if pc else r['artifact_class']=='PTOB_T096_RC7' and r['profile'] in ('GM','RC')) and r['usb_source_stream'] is True,'Unknown profile')
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
    if tuple(map(int,r['version'].split('.'))) >= (0,15,166):
        require(('/protocol/gnss_diagnostics.c' in commands)==(not dc and not pc),
                'GNSS diagnostic codec membership mismatch')
    if not dc and tuple(map(int,r['version'].split('.'))) >= (0,15,101):
        units=json.loads(commands)
        aux=[x['file'] for x in units if x['file'].endswith('/lll_scan_aux.c')]
        require(len(aux)==1 and aux[0].endswith('/r2_radio_controller/lll_scan_aux.c'),
                'Missing/duplicate pinned AUX initiation diagnostic source')
    diagnostic=r.get('controller_diagnostics',False)
    require(type(diagnostic) is bool,'Invalid diagnostic variant flag')
    require(not diagnostic or not (dc or pc),'Diagnostic variant leaked into DC/PC')
    prepare_capture=tuple(map(int,r['version'].split('.'))) >= (0,15,156) and not (dc or pc)
    observer=diagnostic or prepare_capture
    require(('/src/controller_diag.c' in commands)==observer,'Diagnostic observer source mismatch')
    require(('/r2_controller_diag/ull.c' in commands)==observer,'Diagnostic controller hook mismatch')
    require(('R2_CONTROLLER_FAULT_CAPTURE=1' in commands)==prepare_capture,'Prepare capture compiler mode mismatch')
    require(('R2_CONTROLLER_DIAGNOSTICS=1' in commands)==diagnostic,'Diagnostic compiler mode mismatch')
    if tuple(map(int,r['version'].split('.'))) >= (0,15,155):
        fault_diag=r.get('controller_fault_diagnostics')
        if dc or pc:
            require(fault_diag is None,'Fault diagnostics leaked into DC/PC receipt')
            require('/src/controller_snapshot.c' not in commands and '/r2_controller/ll_sw/ull_peripheral.c' not in commands,
                    'Fault diagnostics leaked into DC/PC controller')
        else:
            require(fault_diag=={'manifest_sha256':sha(out/'source/cmake/controller-fault-diag.json'),
                                'patch_sha256':sha(out/'source/cmake/controller-fault-diag.patch')},
                    'Fault diagnostic patch/manifest receipt mismatch')
            manifest=json.loads((out/'source/cmake/controller-fault-diag.json').read_text())
            require(manifest['patch_sha256']==fault_diag['patch_sha256'],'Fault diagnostic patch identity mismatch')
            units=json.loads(commands)
            for suffix,expected in [('ticker/ticker.c','/r2_controller/ticker/ticker.c'),
                                    ('ll_sw/ull_peripheral.c','/r2_controller/ll_sw/ull_peripheral.c'),
                                    ('ll_sw/ull.c','/r2_controller_diag/ull.c' if observer else '/r2_controller/ll_sw/ull.c')]:
                candidates=[c['file'] for c in units if c['file'].endswith('/'+suffix) or
                            suffix=='ll_sw/ull.c' and c['file'].endswith('/r2_controller_diag/ull.c')]
                require(len(candidates)==1 and candidates[0].endswith(expected),'Fault diagnostic controller membership: '+suffix)
            for name,hashes in manifest['files'].items():
                relative=Path(name).relative_to('subsys/bluetooth/controller')
                require(sha(out/'target/r2_controller'/relative)==hashes['after'],'Generated fault diagnostic hash: '+name)
    if tuple(map(int,r['version'].split('.'))) >= (0,15,156):
        prepare=r.get('controller_prepare_capture')
        if dc or pc:
            require(prepare is None,'Prepare capture leaked into DC/PC receipt')
        else:
            mode='verbose' if diagnostic else 'failure-only'
            expected_prepare={'mode':mode,
                'manifest_sha256':sha(out/'source/cmake/controller-prepare-diag.json'),
                'patch_sha256':sha(out/'source/cmake/controller-prepare-fault.patch'),
                'generator_sha256':sha(out/'source/tools/controller_diag_hooks.py')}
            context=tuple(map(int,r['version'].split('.'))) >= (0,15,163) and not diagnostic
            if tuple(map(int,r['version'].split('.'))) >= (0,15,163):
                expected_prepare['lll_context']=None if diagnostic else {
                    'manifest_sha256':sha(out/'source/cmake/controller-prepare-context.json'),
                    'generator_sha256':sha(out/'source/tools/controller_prepare_context.py')}
            require(prepare==expected_prepare,'Prepare observer receipt mismatch')
            if tuple(map(int,r['version'].split('.'))) >= (0,15,163):
                require(('/r2_controller_diag/lll.c' in commands)==context,'LLL context source mode mismatch')
                candidates=[c['file'] for c in json.loads(commands) if c['file'].endswith('/lll/lll.c') or c['file'].endswith('/r2_controller_diag/lll.c')]
                require(len(candidates)==1,'Missing/duplicate LLL core')
                if context:
                    pinned_context=json.loads((out/'source/cmake/controller-prepare-context.json').read_text())
                    require(json.loads((out/'target/r2_controller_diag/lll-context-manifest.json').read_text())==pinned_context,'LLL generated manifest mismatch')
                    require(sha(out/'target/r2_controller_diag/lll.c')==pinned_context['output_sha256'],'LLL context hash mismatch')
                    require(sha(out/'source/cmake/controller-prepare-context.patch')==pinned_context['patch_sha256'],'LLL context patch hash mismatch')
            pinned=json.loads((out/'source/cmake/controller-prepare-diag.json').read_text())
            generated=json.loads((out/'target/r2_controller_diag/manifest.json').read_text())
            require(pinned['failure_patch_sha256']==prepare['patch_sha256'],'Prepare patch hash mismatch')
            require(generated=={'input_sha256':pinned['input_sha256'],
                'output_sha256':pinned['output_sha256'][mode],'class':'PREPARE_FIFO_OBSERVATION_ONLY',
                'mode':mode,'capacity':7,'assertion_unchanged':True,
                'manifest_sha256':prepare['manifest_sha256']},'Prepare generated manifest/mode mismatch')
            require(sha(out/'target/r2_controller_diag/ull.c')==pinned['output_sha256'][mode],
                    'Prepare generated ULL hash mismatch')
    if tuple(map(int,r['version'].split('.'))) >= (0,15,163):
        radio_fixes=r.get('controller_radio_fixes')
        if dc or pc:
            require(radio_fixes is None,'Radio fixes leaked into DC/PC')
            require('/r2_radio_fixes/' not in commands,'Radio fixes source leaked into DC/PC')
        else:
            require(radio_fixes=={'manifest_sha256':sha(out/'source/cmake/controller-radio-fixes.json'),
                'generator_sha256':sha(out/'source/tools/controller_radio_fixes.py')},'Radio fixes receipt mismatch')
            pinned_radio=json.loads((out/'source/cmake/controller-radio-fixes.json').read_text())
            require(json.loads((out/'target/r2_radio_fixes/manifest.json').read_text())==pinned_radio,'Radio fixes generated manifest mismatch')
            require(sha(out/'source/cmake/controller-radio-fixes.patch')==pinned_radio['patch_sha256'],'Combined radio patch identity mismatch')
            for upstream in pinned_radio['upstream']:
                require(sha(out/'source/cmake'/upstream['patch'])==upstream['patch_sha256'],'Upstream radio patch identity mismatch')
            for name,hashes in pinned_radio['files'].items():
                leaf=Path(name).name
                candidates=[c['file']for c in json.loads(commands)if c['file'].endswith('/'+leaf)]
                require(len(candidates)==1 and candidates[0].endswith('/r2_radio_fixes/'+leaf),'Radio fixes source membership: '+leaf)
                require(sha(out/'target/r2_radio_fixes'/leaf)==hashes['output_sha256'],'Radio fixes generated hash: '+leaf)
    dts=(out/'target/zephyr/zephyr.dts').read_text()
    if dc and 'src/dc_gnss.c' in r['sources']:
        require(r.get('dc_gnss_bridge') is True,'Missing DC GNSS bridge receipt')
        require('/src/dc_gnss.c' in commands and '/src/dc_gnss_transport.c' in commands and 'cdc_acm_gnss' in dts,'Missing DC GNSS serial function')
    else:
        require('/src/dc_gnss.c' not in commands and '/src/dc_gnss_transport.c' not in commands and 'cdc_acm_gnss' not in dts,'DC GNSS bridge leaked into GM/RC')
    for feature in [('R2_DC_PROFILE=1' if dc else 'R2_RC_PROFILE=1'),'R2_USB_SOURCE_STREAM=1','R2_USB_SOF_PROBE=1']:
        require(feature in commands,'Missing standard RC7 feature: '+feature)
    if pc:
        require(r.get('awake_only') is True and r.get('profile_toggleable') is False and r.get('relay_transmit') is False,'PC receipt contract')
        require('R2_PC_PROFILE=1' in commands and '/src/pc_profile_service.c' in commands and '/src/pc_experiment.c' in commands,'Missing PC owner')
        require(not any(x in commands for x in ['/src/rc_profile_service.c','/src/ble_service.c','/src/radio_service.c','/src/relay_service.c','/src/direct_connect.c']),'PC contains ordinary profile owners')
        enrollment=(out/'r2_enrollment.h').read_text()
        auto=r.get('pc_auto_direct_gm',False)
        require(auto == (r['pc_parent']=='auto'),'PC automatic-parent receipt mismatch')
        parent='0000000000000000' if auto else r['pc_parent']
        require('.rc7_leaf=true' in enrollment and '.rc7_boot_rc=true' in enrollment and
                '.rc7_boot_parent=UINT64_C(0x'+parent+')' in enrollment,'PC leaf/parent binding')
        require(('R2_PC_AUTO_PARENT=1' in commands)==auto,'PC automatic-parent build mismatch')
        with (out/'target/zephyr/zephyr.elf').open('rb') as f:
            symbols={s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        require(not any(x in symbols for x in ['r2_normal_service_configure','r2_normal_service_run','r2_radio_service_gm_start','r2_dc_poll']),'PC linked ordinary timing owner')
        require('CONFIG_CLOCK_CONTROL_NRF_K32SRC_SYNTH=y' in config,'PC awake clock configuration changed')
    else:
        require('R2_PC_PROFILE=1' not in commands and '/src/pc_' not in commands,'PC leaked into ordinary image')
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
    private_sha=sha(out/'r2_enrollment.h')
    transmitter=r.get('lora_transmitter_factory','0000000000000000')
    require(isinstance(transmitter,str) and re.fullmatch('[0-9a-f]{16}',transmitter),'Invalid LoRa binding')
    if int(transmitter,16):
        require(not dc and not pc,'LoRa binding outside ordinary GM/RC')
        entry=json.loads((out/'registry.json').read_text())['devices'].get(transmitter)
        require(entry is not None and entry['target']=='t096','LoRa TX factory missing T096 metadata')
        macro='#define R2_LORA_TRANSMITTER_FACTORY UINT64_C(0x'+transmitter+')'
        require(macro in (out/'target/build_identity.h').read_text(),'LoRa private binding mismatch')
        require('/src/lora_hw.c' in commands and 'R2_LORA=1' in commands,'Missing LoRa owner')
        require('CONFIG_NRFX_SPIM1=y' in config and 'CONFIG_NRFX_TIMER4=y' in config,'Missing LoRa peripheral owner')
        # The measured0.15.122 normal-image margin is8740B. Preserve the
        # separately agreed4096B incremental RC8 ceiling as well as1024B gate.
        require(0x20040000-ram_end>=8740-4096,'RC8 exceeds4096B incremental static budget')
        private_sha=hashlib.sha256((private_sha+':lora-tx:'+transmitter).encode()).hexdigest()
    if dc or pc:
        with (out/'target/zephyr/zephyr.elf').open('rb') as f:
            names={s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        require(not any(s.startswith('r2_lora_') for s in names),'LoRa owner leaked into DC/PC')
        require('CONFIG_NRFX_SPIM1=y' not in config and 'CONFIG_NRFX_TIMER4=y' not in config,'LoRa peripherals leaked into DC/PC')
    if diagnostic:private_sha=hashlib.sha256((private_sha+':controller-diag:1').encode()).hexdigest()
    binding=hashlib.sha256((r['source_sha256']+':'+private_sha+':'+sha(out/'target/zephyr/.config')).encode()).hexdigest()
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
