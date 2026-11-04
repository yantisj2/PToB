#!/usr/bin/env python3
"""Offline source/image/layout validation; not deployment authorization.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,contextlib,hashlib,importlib.util,json,re,struct,sys,tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile
from uf2 import verify
from tz_compile_mode import require_tz_compile_mode

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)

def crash_static_allowance(gm7,profile,version,config,symbols):
    """Separate the .180-.186 GM diagnostic allocation from the RC8 ceiling.

    Fail closed for other images/versions. The allowance is earned by the
    linked allocations, not by a receipt flag or an estimated C object size.
    """
    if not(gm7 and profile=='GM' and version in ((0,15,180),(0,15,181),(0,15,182),(0,15,183),(0,15,184),(0,15,185),(0,15,186))):return 0
    for line in ('CONFIG_ISR_STACK_SIZE=3072','CONFIG_MP_MAX_NUM_CPUS=1',
                 'CONFIG_MPU_STACK_GUARD=y'):
        require(line in config.splitlines(),'Crash allocation configuration: '+line)
    for name,size in (('r2_crash_record',512),('z_interrupt_stacks',3136)):
        entries=symbols.get(name,[])
        require(len(entries)==1,'Missing/duplicate crash allocation: '+name)
        q=entries[0]
        require(q['size']==size and q['section']=='noinit' and
                q['type']=='SHT_NOBITS' and
                0x20002000<=q['address']<=0x20040000-size,
                'Invalid linked crash allocation: '+name)
    if version in ((0,15,182),(0,15,183),(0,15,184),(0,15,185),(0,15,186)):
        for name,size in (('pt_input',1128),('pt_output',728)):
            entries=symbols.get(name,[])
            require(len(entries)==1,'Missing/duplicate PT mailbox: '+name)
            q=entries[0]
            require(q['size']==size and q['type']=='SHT_NOBITS' and
                    0x20002000<=q['address']<=0x20040000-size,
                    'GM7 recovery must retain linked PT mailbox size: '+name)
    # One CPU, actual64B guard/prefix:3136-(2176+64)=896 new ISR bytes.
    return symbols['r2_crash_record'][0]['size']+symbols['z_interrupt_stacks'][0]['size']-(2176+64)

def inspect(out,allow_development=False):
    out=Path(out);r=json.loads((out/'receipt.json').read_text())
    dc=r['profile']=='DC'
    pc=r['profile']=='PC'
    gm7=r.get('gm7_native',False)
    slow=r.get('lora_passive_slow',False)
    passive=r.get('lora_passive_fast',False) or slow
    require(type(passive) is bool and (not passive or gm7 and not(dc or pc)), 'Invalid passive variant')
    executable_end=0xd7000 if passive else 0xdf000
    require(type(gm7) is bool and not (gm7 and (dc or pc)),'Invalid GM7 profile selection')
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
    data=image.read_bytes();require(0x26000+len(data)//512*256<=executable_end,'UF2 enters credential reservation')
    payload=b''.join(data[i+32:i+288] for i in range(0,len(data),512))
    binary=(out/'target/zephyr/zephyr.bin').read_bytes()
    require(payload[:len(binary)]==binary and all(x==0 for x in payload[len(binary):]),'UF2/BIN mismatch')
    flash_end=0;ram_end=0;allocations={};ui_mutable_bytes=0;linked_names=set();holdover_mutable_bytes=0;audit_mutable_bytes=0
    lora_display_mutable={name:0 for name in (('main.c','ui_service.c','lora_passive_service.c','lora_hw.c') if passive else ('main.c','ui_service.c','lora_service.c','lora_hw.c'))}
    lab_audit_bytes=0
    if slow:lora_display_mutable['gm7_admission']=0
    with (out/'target/zephyr/zephyr.elf').open('rb') as f:
        elf=ELFFile(f)
        for seg in elf.iter_segments():
            if seg['p_type']!='PT_LOAD':continue
            if seg['p_filesz']:
                require(0x26000<=seg['p_paddr'] and seg['p_paddr']+seg['p_filesz']<=executable_end,'ELF load bounds')
                flash_end=max(flash_end,seg['p_paddr']+seg['p_filesz'])
            if seg['p_vaddr']>=0x20000000 and seg['p_memsz']:
                require(0x20002000<=seg['p_vaddr'] and seg['p_vaddr']+seg['p_memsz']<=0x20040000,'ELF RAM bounds')
                ram_end=max(ram_end,seg['p_vaddr']+seg['p_memsz'])
        symbol_owner=''
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            linked_names.add(symbol.name)
            if symbol['st_info']['type']=='STT_FILE':symbol_owner=symbol.name
            index=symbol['st_shndx']
            if symbol_owner=='rc_dynamic_service.c' and symbol.name.startswith('hold_') and symbol['st_info']['type']=='STT_OBJECT' and isinstance(index,int):
                if elf.get_section(index)['sh_flags']&1:holdover_mutable_bytes+=int(symbol['st_size'])
            if symbol_owner=='radio_audit.c' and symbol['st_info']['type']=='STT_OBJECT' and isinstance(index,int):
                if elf.get_section(index)['sh_flags']&1:audit_mutable_bytes+=int(symbol['st_size'])
            if symbol_owner=='ui_service.c' and symbol['st_info']['type']=='STT_OBJECT' and isinstance(index,int):
                if elf.get_section(index)['sh_flags']&1:ui_mutable_bytes+=int(symbol['st_size'])
            if slow and symbol_owner=='gm7_service.c' and symbol.name in ('lora_admitted_epoch','lora_readmission'):
                require(symbol['st_size']>0 and elf.get_section(index)['sh_flags']&1,'GM7 LoRa admission allocation')
                lora_display_mutable['gm7_admission']+=int(symbol['st_size'])
            if symbol_owner in lora_display_mutable and symbol['st_info']['type']=='STT_OBJECT' and isinstance(index,int):
                if elf.get_section(index)['sh_flags']&1:lora_display_mutable[symbol_owner]+=int(symbol['st_size'])
            if r.get('lora_bw_experiment') and symbol_owner=='lora_passive_service.c' and symbol.name=='audits':
                require(isinstance(index,int) and elf.get_section(index)['sh_flags']&1,'Missing writable laboratory packet audits')
                lab_audit_bytes+=int(symbol['st_size'])
            if symbol.name not in ('r2_fault_capsule','r2_watchdog_capsule','r2_crash_record','z_interrupt_stacks','pt_input','pt_output','r2_tz_data','tz_selector'):continue
            index=symbol['st_shndx']
            require(isinstance(index,int),'Unallocated crash symbol')
            section=elf.get_section(index)
            allocations.setdefault(symbol.name,[]).append(dict(
                size=int(symbol['st_size']),address=int(symbol['st_value']),
                section=section.name,type=section['sh_type']))
    config=(out/'target/zephyr/.config').read_text()
    if gm7 and r['profile'] in ('GM','RC') and tuple(map(int,r['version'].split('.')))>=(0,15,269):
        require(1000<=holdover_mutable_bytes<=1280,'Complete local HOLD state exceeds1280B or is missing')
    if tuple(map(int,r['version'].split('.')))>=(0,15,271) and not dc:
        require(0<audit_mutable_bytes<=1108,'Shared audit capture exceeds its existing1108B mutable storage')
    crash_allowance=crash_static_allowance(gm7,r['profile'],tuple(map(int,r['version'].split('.'))),config,allocations)
    # .194 adds64B of linked HOT retention/transition diagnostics (including alignment).
    # Account for it separately from the unchanged RC8 ceiling and absolute RAM floor.
    # .197 adds one durable callback word per slot (24B with struct alignment).
    hot_allowance=(104 if tuple(map(int,r['version'].split('.'))) >= (0,15,197) else 80 if tuple(map(int,r['version'].split('.'))) >= (0,15,196) else 64) if gm7 and tuple(map(int,r['version'].split('.'))) >= (0,15,194) else 0
    commands=(out/'target/compile_commands.json').read_text()
    if tuple(map(int,r['version'].split('.')))>=(0,15,288):
        usb=r.get('usb_cdc_recovery')
        if dc or pc:
            require(usb is None and '/r2_usb_cdc_recovery/' not in commands,'USB recovery leaked into DC/PC')
        else:
            require(usb=={'manifest_sha256':sha(out/'source/cmake/usb-cdc-recovery.json'),
                'generator_sha256':sha(out/'source/tools/usb_cdc_recovery.py'),
                'patch_sha256':sha(out/'source/cmake/usb-cdc-recovery.patch')},'USB recovery receipt')
            pinned_usb=json.loads((out/'source/cmake/usb-cdc-recovery.json').read_text())
            require(json.loads((out/'target/r2_usb_cdc_recovery/manifest.json').read_text())==pinned_usb,'USB generated manifest')
            require(sha(out/'source/cmake/usb-cdc-recovery.patch')==pinned_usb['patch_sha256'],'USB patch hash')
            for entry in pinned_usb['files']:
                candidates=[c['file']for c in json.loads(commands)if c['file'].endswith('/'+entry['leaf'])]
                require(len(candidates)==1 and candidates[0].endswith('/r2_usb_cdc_recovery/'+entry['leaf']),'Unique USB compiled owner: '+entry['leaf'])
                require(sha(out/'target/r2_usb_cdc_recovery'/entry['leaf'])==entry['output_sha256'],'USB generated output hash')
            require('R2_USB_CDC_RECOVERY=1' in commands,'Periodic console RX readiness integration')
    if gm7 and tuple(map(int,r['version'].split('.'))) >= (0,15,202):
        require_tz_compile_mode(json.loads(commands))
    # TZ1 wire/cache allocation is208B versus the inspected .201 GM/RC image.
    # Source205 adds a bounded64B for leased TZ snapshots used by the UI.
    # Charge it to its own bounded feature allocation, not the RC8 allowance.
    # Radio/USB buffers, stacks and the absolute1024B floor stay unchanged.
    tz_allowance=(272 if tuple(map(int,r['version'].split('.'))) >= (0,15,205) else 208) if gm7 and 'R2_TZ_WIRE=1' in commands else 0
    auto_tz_allowance=256 if gm7 and 'R2_TZ_AUTO=1' in commands else 0
    # .210 UI mutable symbols total18145B (including the unchanged12800B
    # framebuffer). Bound the new menu/control state separately from TZ/RC8;
    # existing stacks, buffers and the absolute1024B floor do not increase.
    menu_allowance=128 if gm7 and tuple(map(int,r['version'].split('.')))>=(0,15,211) else 0
    # .213 own-device GM/RC ELF writable totals: main3711, UI18193,
    # LoRa service1247, driver455 =23606B. .215 adds bounded OTA-name,
    # actual packet strength/activity and coarse-display/fit provenance state.
    # Count all affected linked objects (including every copied model), cap the
    # whole addition at256B, and retain the original RC8/TZ/menu/stack floors.
    lora_display_allowance=256 if gm7 and tuple(map(int,r['version'].split('.')))>=(0,15,215) and int(r.get('lora_transmitter_factory','0'),16) else 0
    # .215 actual GM/RC affected objects total23795B. Bound the entire new
    # half-duplex/probe/TZ/rate addition, including all copied UI snapshots.
    lora_stage2_allowance=(1536 if passive and tuple(map(int,r['version'].split('.')))>=(0,15,251) else 512) if lora_display_allowance and tuple(map(int,r['version'].split('.')))>=(0,15,216) else 0
    # .272's exact affected-object increment is1510B. Schedule3 adds only
    # sparse-fit scalars and a copied committed model, with no history array,
    # stack or buffer increase. Charge this separately, bounded at128B; the
    # original stage2 ceiling and absolute1024B RAM floor remain visible.
    lora_enhanced_allowance=128 if (slow or r.get('lora_bw_experiment')) and tuple(map(int,r['version'].split('.')))>=(0,15,273) else 0
    # LAB fast keeps4x48B audits; LAB slow keeps1x48B. Compare with the
    # unchanged40B SLOW baseline and bound each explicit profile increment.
    lab_audit_allowance=(8 if slow else 160) if r.get('lora_bw_experiment') else 0
    if lab_audit_allowance:require(lab_audit_bytes==(48 if slow else 192),'Laboratory selected-profile audit allocation')
    lab_audit_added=max(0,lab_audit_bytes-40) if lab_audit_allowance else 0
    require(lab_audit_added<=lab_audit_allowance,'Laboratory audit exceeds its selected-profile increment')
    lora_stage2_added=max(0,sum(lora_display_mutable.values())-23795)
    lora_enhanced_added=max(0,lora_stage2_added-1510-lab_audit_added) if lora_enhanced_allowance else 0
    require(lora_enhanced_added<=lora_enhanced_allowance,'Sparse LoRa servo exceeds128B added native state')
    if lora_stage2_allowance:
        require(lora_stage2_added<=lora_stage2_allowance+lora_enhanced_allowance+lab_audit_allowance,'LoRa runtime exceeds its bounded mutable state allocation: '+str(lora_display_mutable))
        require((({'lp_decode','lp_tx_reserve_control','lp_rx_follow','lp_tx_start','r2_lora_hw_passive_configure','r2_lora_shadow_publish'}| (set() if r.get('lora_tx_diagnostic') else {'lp_encode'})) if passive else {'rc8_probe_encode','rc8_probe_decode','r2_lora_hw_idle','r2_lora_tz_publish'})<=linked_names,'Missing selected LoRa integration')
    lora_display_added=max(0,sum(lora_display_mutable.values())-23606)
    if lora_display_allowance:
        require(({'r2_lora_name_publish','lp_context'} if passive else {'r2_lora_name_publish','rc8_name_encode','rc8_name_decode'})<=linked_names,'Missing authenticated LoRa metadata')
        require(all(lora_display_mutable.values()),'Missing linked LoRa display allocation accounting')
        require(lora_display_added<=lora_display_allowance+lora_stage2_allowance+lora_enhanced_allowance+lab_audit_allowance,'LoRa name/display state exceeds bounded addition')
    if menu_allowance:
        require({'r2_ui_press','r2_ui_menu_result','r2_service_ble_rx'}<=linked_names,'Missing ordinary menu/receive controls')
        require(0<ui_mutable_bytes<=18145+menu_allowance+lora_stage2_allowance,'Menu exceeds128B added mutable UI state')
    if auto_tz_allowance:
        require(tuple(map(int,r['version'].split('.')))>=(0,15,206),'Unexpected automatic TZ version')
        data=allocations.get('r2_tz_data',[]);state=allocations.get('tz_selector',[])
        require(len(data)==1 and 0<data[0]['size']<=16384,'Automatic TZ flash data budget')
        require(len(state)==1 and state[0]['size']<=40,'Automatic TZ selector state budget')
        with (out/'target/zephyr/zephyr.elf').open('rb') as stream:
            selected=ELFFile(stream);symbol=data[0]
            section=selected.get_section_by_name(symbol['section'])
            require(section is not None and not (section['sh_flags']&1),'TZ image must be read-only')
            begin=symbol['address']-section['sh_addr']
            image=section.data()[begin:begin+symbol['size']]
            require(len(image)==symbol['size'],'Truncated linked TZ image')
        require(hashlib.sha256(image).hexdigest()==r.get('time_zone',{}).get('database_sha256'),'Linked TZ image identity mismatch')
        # .205 same normal configuration leaves4372B. Charge at most256B for
        # the complete auto feature, including position, queue and snapshots.
        require(0x20040000-ram_end+menu_allowance+lora_display_allowance+lora_stage2_allowance>=4372-256,'Automatic TZ exceeds256B added static state')
        require(r.get('time_zone',{}).get('mode') in ('auto','manual-zone','manual-offset'),'Missing TZ build binding')
        tz=r['time_zone']
        require(tz['mode_number']=={'manual-offset':0,'manual-zone':1,'auto':2}[tz['mode']],'TZ mode binding disagreement')
        for option,key in [('MODE','mode_number'),('MANUAL_ID','id'),('MANUAL_MIN','offset_min')]:
            require('-DR2_TZ_'+option+'='+str(tz[key]) in commands,'TZ compiled configuration disagreement')
    if not dc and tuple(map(int,r['version'].split('.'))) >= (0,15,63):
        rx=re.search(r'^CONFIG_BT_CTLR_RX_BUFFERS=(\d+)$',config,re.M)
        work=re.search(r'^CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=(\d+)$',config,re.M)
        require(rx is not None and int(rx[1])>=8,'Insufficient native RX reserve')
        require(work is not None and int(work[1])>=5120,'Insufficient workqueue stack')
        require(0x20040000-ram_end>=1024,'Insufficient static RAM margin')
    if gm7:
        isr=re.search(r'^CONFIG_ISR_STACK_SIZE=(\d+)$',config,re.M)
        require(isr is not None and int(isr[1])>=(3072 if tuple(map(int,r['version'].split('.'))) >= (0,15,180) else 2176),'Insufficient GM7 ISR stack')
        if tuple(map(int,r['version'].split('.'))) >= (0,15,180):
            require('# CONFIG_FPU is not set' in config,'Crash capture requires FPU disabled')
        if tuple(map(int,r['version'].split('.'))) >= (0,15,175):
            require('CONFIG_EXTRA_EXCEPTION_INFO=y' in config,'Missing GM7 integer fault capture')
    for line in [('CONFIG_ROM_END_OFFSET=0' if passive else 'CONFIG_ROM_END_OFFSET=0x6000'),'CONFIG_FLASH=y','CONFIG_USB_DEVICE_STACK=y','CONFIG_WATCHDOG=y']:
        require(line in config,'Required configuration: '+line)
    if gm7 and tuple(map(int,r['version'].split('.'))) >= (0,15,188):
        require(sum(q['file'].endswith('/src/gm7_snapshot_witness.S')for q in json.loads(commands))==1,'Missing/duplicate snapshot witness translation unit')
    if tuple(map(int,r['version'].split('.'))) >= (0,15,166):
        require(('/protocol/gnss_diagnostics.c' in commands)==(not dc and not pc),
                'GNSS diagnostic codec membership mismatch')
    if not dc and tuple(map(int,r['version'].split('.'))) >= (0,15,101):
        units=json.loads(commands)
        aux=[x['file'] for x in units if x['file'].endswith('/lll_scan_aux.c')]
        require(len(aux)==1 and aux[0].endswith('/gm7_controller/lll_scan_aux.c' if gm7 else '/r2_radio_controller/lll_scan_aux.c'),
                'Missing/duplicate pinned AUX initiation diagnostic source')
    diagnostic=r.get('controller_diagnostics',False)
    require(type(diagnostic) is bool,'Invalid diagnostic variant flag')
    require(not diagnostic or not (dc or pc),'Diagnostic variant leaked into DC/PC')
    prepare_capture=tuple(map(int,r['version'].split('.'))) >= (0,15,156) and not (dc or pc)
    observer=diagnostic or prepare_capture
    require(('/src/controller_diag.c' in commands)==observer,'Diagnostic observer source mismatch')
    require(('/gm7_controller/ull.c' in commands if gm7 else '/r2_controller_diag/ull.c' in commands)==observer,'Diagnostic controller hook mismatch')
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
                                    ('ll_sw/ull.c','/gm7_controller/ull.c' if gm7 else '/r2_controller_diag/ull.c' if observer else '/r2_controller/ll_sw/ull.c')]:
                candidates=[c['file'] for c in units if c['file'].endswith('/'+suffix) or
                            suffix=='ll_sw/ull.c' and c['file'].endswith('/gm7_controller/ull.c' if gm7 else '/r2_controller_diag/ull.c')]
                require(len(candidates)==1 and candidates[0].endswith(expected),'Fault diagnostic controller membership: '+suffix)
            for name,hashes in manifest['files'].items():
                relative=Path(name).relative_to('subsys/bluetooth/controller')
                generated=(out/'target/r2_controller'/relative).read_bytes()
                if name.endswith('ticker/ticker.c') and tuple(map(int,r['version'].split('.'))) >= (0,15,194):
                    # Verify the upstream correction, then the pinned diagnostic body.
                    before=b'while (user->first != user->last)'
                    after=b'while (user->first != user->middle)'
                    require(generated.count(after)==1 and before not in generated,'Ticker inquiry boundary correction missing')
                    generated=generated.replace(after,before)
                require(hashlib.sha256(generated).hexdigest()==hashes['after'],'Generated fault diagnostic hash: '+name)
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
    if tuple(map(int,r['version'].split('.'))) >= (0,15,190):
        aux=r.get('scan_aux_context')
        if dc or pc:
            require(aux is None and '/r2_scan_aux_context/' not in commands,'Periodic AUX correction leaked into DC/PC')
        else:
            require(aux=={'manifest_sha256':sha(out/'source/cmake/scan-aux-context.json'),
                'generator_sha256':sha(out/'source/tools/scan_aux_context.py'),
                'patch_sha256':sha(out/'source/cmake/scan-aux-context.patch')},'Periodic AUX correction receipt')
            pinned_aux=json.loads((out/'source/cmake/scan-aux-context.json').read_text())
            require(json.loads((out/'target/r2_scan_aux_context/manifest.json').read_text())==pinned_aux,'Periodic AUX generated manifest')
            require(sha(out/'source/cmake/scan-aux-context.patch')==pinned_aux['patch_sha256'],'Periodic AUX patch hash')
            candidates=[c['file']for c in json.loads(commands)if c['file'].endswith('/ull_scan_aux.c')]
            require(len(candidates)==1 and candidates[0].endswith('/r2_scan_aux_context/ull_scan_aux.c'),'Unique periodic AUX source')
            require(sha(out/'target/r2_scan_aux_context/ull_scan_aux.c')==pinned_aux['output_sha256'],'Periodic AUX output hash')
            require('CONFIG_BT_CTLR_SYNC_PERIODIC=y' in config.splitlines() and 'CONFIG_BT_CTLR_SCAN_AUX_USE_CHAINS=y' not in config.splitlines(),'Reviewed non-chain periodic AUX configuration')
    if gm7:
        require(r.get('gm7_controller')=={
            'manifest_sha256':sha(out/'source/cmake/gm7-controller.json'),
            'generator_sha256':sha(out/'source/tools/gm7_controller_hooks.py')},'GM7 generator/manifest binding')
        gm7_inputs=[out/'target/r2_controller_diag/ull.c',
                    out/'target/r2_radio_controller/lll_scan.c',
                    out/'target/r2_radio_controller/lll_scan_aux.c',
                    out/'target/r2_precision_controller/ull_adv_sync.c']
        spec=importlib.util.spec_from_file_location('gm7_bound_generator',out/'source/tools/gm7_controller_hooks.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='ptob-gm7-inspect-') as tmp:
            hci_input=Path(tmp)/'input/hci_driver.c'
            hci_input.parent.mkdir()
            hci_input.write_bytes((out/'target/gm7_controller/hci_driver.input').read_bytes())
            generated=module.generate(*gm7_inputs,Path(tmp),hci_input)
            require(generated==json.loads((out/'target/gm7_controller/manifest.json').read_text()),'GM7 composed controller manifest')
            for leaf,entry in generated.items():
                require(sha(out/'target/gm7_controller'/leaf)==entry['output_sha256'],'GM7 exact composed hook: '+leaf)
                candidates=[c['file']for c in json.loads(commands)if c['file'].endswith('/'+leaf)]
                require(len(candidates)==1 and candidates[0].endswith('/gm7_controller/'+leaf),'GM7 unique compiled owner: '+leaf)
        if 'tools/flash_thread_guard.py' in r['sources']:
            require(r.get('flash_thread_guard')=={'generator_sha256':sha(out/'source/tools/flash_thread_guard.py')},'Flash THREAD guard generator binding')
            require('CONFIG_SOC_FLASH_NRF_RADIO_SYNC_TICKER=y' in config.splitlines(),'Native GM flash synchronization backend')
            spec=importlib.util.spec_from_file_location('flash_thread_bound_generator',out/'source/tools/flash_thread_guard.py')
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            with tempfile.TemporaryDirectory(prefix='ptob-flash-thread-inspect-') as tmp:
                original=Path(tmp)/'input/soc_flash_nrf_ticker.c';original.parent.mkdir()
                original.write_bytes((out/'target/gm7_flash/soc_flash_nrf_ticker.input').read_bytes())
                generated=module.generate(original,Path(tmp)/'output')
                require(generated==json.loads((out/'target/gm7_flash/manifest.json').read_text()),'Flash THREAD generated manifest')
                require(sha(out/'target/gm7_flash/soc_flash_nrf_ticker.c')==generated['output_sha256'],'Flash THREAD exact composed source')
                candidates=[c['file']for c in json.loads(commands)if c['file'].endswith('/soc_flash_nrf_ticker.c')]
                require(len(candidates)==1 and candidates[0].endswith('/gm7_flash/soc_flash_nrf_ticker.c'),'Flash THREAD unique compiled owner')
        for leaf in ['gm7_service.c','gm7_controller.c','gm7_clock.c','gm7_capture.c','gm7_owner.c','gm7_control.c','gm7_codec.c']:
            require(sum(c['file'].endswith('/'+leaf)for c in json.loads(commands))==1,'GM7 production membership: '+leaf)
        require('/src/ble_service.c' not in commands,'Retired GM coordinator owner compiled in GM7')
        require('CONFIG_BT_CTLR_PHY_2M=y' not in config,'GM7 management PHY changed ordinary ACL feature mask')
        with (out/'target/zephyr/zephyr.elf').open('rb') as f:
            symbols={s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        require('r2_gm7_controller_submit' in symbols and 'r2_network_tick' not in symbols,'GM7 live owner closure')
    else:
        require(r.get('gm7_controller') is None and r.get('flash_thread_guard') is None and '/gm7_controller/' not in commands and '/gm7_flash/' not in commands,'GM7 backend leaked into separate image')
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
        # .180-.186 GM have a separately verified512B retained crash record and
        #896B ISR allocation; neither changes the RC8 budget or absolute floor.
        require(0x20040000-ram_end+crash_allowance+hot_allowance+tz_allowance+auto_tz_allowance+menu_allowance+lora_display_allowance+lora_stage2_allowance>=8740-4096,'RC8 exceeds4096B incremental static budget')
        private_sha=hashlib.sha256((private_sha+':lora-tx:'+transmitter).encode()).hexdigest()
    if dc or pc:
        with (out/'target/zephyr/zephyr.elf').open('rb') as f:
            names={s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
        require(not any(s.startswith('r2_lora_') for s in names),'LoRa owner leaked into DC/PC')
        require('CONFIG_NRFX_SPIM1=y' not in config and 'CONFIG_NRFX_TIMER4=y' not in config,'LoRa peripherals leaked into DC/PC')
    if passive:
        profile='RAM_30_120_300' if slow else 'RAM_LAB_4_12'
        if slow:
            fixed=r.get('lora_fixed500_experiment',False)
            require(r.get('passive_schedule')==dict(id=8 if fixed else 7 if r.get('lora_bw_experiment') else 3,precision_us=30000000,discovery_us=0 if fixed else 120000000,
                    metadata_us=300000000,rolling_hour_cap_us=90000000 if fixed else 108000000),'Enhanced passive schedule/cap binding')
        require(r.get('secondary_lora_class')==('RAM_FIXED500_30_300' if r.get('lora_fixed500_experiment') else ('RAM_BW_SWEEP_30' if slow else 'RAM_BW_AB_4_12') if r.get('lora_bw_experiment') else 'RAM_DIAG_4_12' if r.get('lora_tx_diagnostic') else profile) and ('R2_LORA_PASSIVE_SLOW=1' if slow else 'R2_LORA_PASSIVE_FAST=1') in commands, 'Passive profile binding')
        require(r.get('runtime_state')=='RAM_ONLY' and r.get('passive_wire_version')==3,'RAM/wire cohort receipt')
        require(not ({'lp_store_load','lp_store_save','lp_store_prepare','lp_tx_restore','lp_checkpoint_carry'}&linked_names),'Persistence leaked into native LoRa owner')
        require(int(r['lora_backup_transmitter_factory'],16)==0,'Automatic backup emitter prohibited')
        # Application compilation units may not reference any NV driver API.
        # Zephyr's generic flash driver can remain linked; it is not a writer caller.
        import subprocess
        entries=json.loads(commands)
        compiler=(entries[0].get('arguments') or __import__('shlex').split(entries[0]['command']))[0]
        nm=str(Path(compiler).with_name('arm-zephyr-eabi-nm'))
        archive=out/'target/app/libapp.a'
        undefined=subprocess.check_output([nm,'-u',str(archive)],text=True)
        require(not re.search(r'(?:flash_(?:read|write|erase)|nvs_(?:read|write|delete)|settings_(?:load|save|delete))\b',undefined),'Application NV dependency remains')
        for name in ('r2_fault_capsule','r2_crash_record','r2_watchdog_capsule'):
            for q in allocations.get(name,[]):require(q['section']!='noinit','Operational retained state: '+name)
        require('rc8_probe_encode' not in linked_names and 'rc8_name_encode' not in linked_names,'Active RC8 companion leaked into passive image')
        require('/src/lora_service.c' not in commands and '/src/lora_passive_store.c' not in commands,'Passive owner membership')
        dts=(out/'target/zephyr/zephyr.dts').read_text()
        require('passive_lora_state' in dts and '0xd7000' in dts and '0x8000' in dts,'Missing dedicated passive ledger partition')
        suffix=':lora-passive:'+profile
        private_sha=hashlib.sha256((private_sha+suffix).encode()).hexdigest()
    if r.get('lora_tx_diagnostic'):
        require(not slow and r['profile']=='GM' and json.loads((out/'device.json').read_text())['device_id']==transmitter and 'R2_LORA_TX_DIAGNOSTIC=1' in commands,'Diagnostic TX exact/profile binding')
        private_sha=hashlib.sha256((private_sha+':tx-diagnostic:1').encode()).hexdigest()
    else:require('R2_LORA_TX_DIAGNOSTIC=1' not in commands,'Unexpected diagnostic TX')
    if r.get('lora_fixed500_experiment'):
        require(slow and not r.get('lora_bw_experiment') and not r.get('lora_tx_diagnostic') and 'R2_LORA_FIXED500_EXPERIMENT=1' in commands,'Fixed500 experiment binding')
        event_delay=r.get('fixed500_experiment',{}).get('event_delay_ns')
        require(type(event_delay) is int and abs(event_delay)<=1000000000,'Fixed500 event correction range')
        require(set(re.findall(r'R2_LORA_FIXED500_EVENT_DELAY_NS=(-?\d+)(?!\d)',commands))=={str(event_delay)},'Fixed500 native event correction binding')
        require(r.get('fixed500_experiment')==dict(frequency_hz=922400000,sf=11,bandwidth_hz=500000,
            coding_rate='4/8',preamble=12,explicit_header=True,crc=True,source_hold_us=5000000,
            rx='CONTINUOUS_HEADER_VALID_CRC_CONFIRMED',event_delay_ns=event_delay,default_on=True),'Fixed500 receipt')
        private_sha=hashlib.sha256((private_sha+':fixed500:922400000_SF11_CR8_P30_M300').encode()).hexdigest()
        private_sha=hashlib.sha256((private_sha+':event-delay-ns:'+str(event_delay)).encode()).hexdigest()
        if tuple(map(int,r['version'].split('.')))>=(0,15,288):
            require(r.get('fixed500_runtime')==dict(ble_return_dwell_us=30000000,deferred_fault_ms=300000,
                precharge_lead_us=4000000,meta_launch_late_us=3000000,operator_rearm=True,
                source_gap='EXACT_AUTHENTICATED_SCOPE_FUTURE_PRECISION',normal_export_authority=False),'Fixed500 runtime policy binding')
            require({'lp_tx_rearm','r2_lora_ble_observe','r2_lora_ble_shadow_allowed'}<=linked_names,'Fixed500 runtime integration')
    else:require('R2_LORA_FIXED500_EXPERIMENT=1' not in commands,'Unexpected fixed500 experiment')
    if r.get('lora_bw_experiment'):
        require(passive and not r.get('lora_tx_diagnostic') and 'R2_LORA_BW_EXPERIMENT=1' in commands,'Bandwidth experiment profile binding')
        expected_experiment=dict(schedule=7 if slow else 5,arm_us=600000000 if slow else 240000000,one_arm_per_boot=True,common_grid_phase_us=400000,
            default_on=False,precision_sf=7,precision_bandwidth_hz=[41667,500000],
            assignment='ABBA_CURSOR_DIV2',control='SF12_BW500',hopping='UNCHANGED_64',
            accounting='CONSERVATIVE_NARROW_BOTH_ARMS',purpose='RAW_ENDPOINT_CHARACTERIZATION_ONLY')
        if slow:expected_experiment.update(precision_bandwidth_hz=[41667,62500,125000,250000,500000],assignment='FWD_REV_CURSOR_DIV2',hopping='FIXED_2_ACTUAL_ACCOUNTING',precision_channels=[16,48],arm_phy=[2,4,5,6,3],precision_us=30000000,sync_offset_us=7000000,follow_offset_us=2500000,follow_latest_us=4500000,frame_us=15000000,source_hold_us=5000000)
        require(r.get('bandwidth_experiment')==expected_experiment,'Bandwidth experiment receipt')
        private_sha=hashlib.sha256((private_sha+(':bw-experiment:SWEEP_SF7_SLOW30_600S_FIXED16_48' if slow else ':bw-experiment:ABBA_SF7_240S')).encode()).hexdigest()
    else:require('R2_LORA_BW_EXPERIMENT=1' not in commands,'Unexpected bandwidth experiment')
    if diagnostic:private_sha=hashlib.sha256((private_sha+':controller-diag:1').encode()).hexdigest()
    binding=hashlib.sha256((r['source_sha256']+':'+private_sha+':'+sha(out/'target/zephyr/.config')).encode()).hexdigest()
    require(binding==r['watchdog_binding_sha256'],'Watchdog binding')
    return {'lab_audit_mutable_bytes':lab_audit_bytes,'lab_audit_added_mutable_bytes':lab_audit_added,'lab_audit_increment_budget_bytes':lab_audit_allowance,'lora_enhanced_added_mutable_bytes':lora_enhanced_added,'lora_enhanced_mutable_budget_bytes':lora_enhanced_allowance,'audit_mutable_bytes':audit_mutable_bytes,'audit_mutable_budget_bytes':1108,'holdover_mutable_bytes':holdover_mutable_bytes,'holdover_mutable_budget_bytes':1280,'status':'PASS','flash_load_end':flash_end,'ram_allocated_end':ram_end,'static_unallocated_bytes':0x20040000-ram_end,'crash_diagnostic_static_allowance_bytes':crash_allowance,'rc8_incremental_static_budget_bytes':4096,'hot_diagnostic_static_budget_bytes':hot_allowance,'tz_static_budget_bytes':tz_allowance,'auto_tz_static_budget_bytes':auto_tz_allowance,'menu_static_budget_bytes':menu_allowance,'ui_mutable_symbol_bytes':ui_mutable_bytes,'lora_stage2_static_budget_bytes':lora_stage2_allowance,'lora_stage2_added_mutable_bytes':lora_stage2_added,'lora_display_static_budget_bytes':lora_display_allowance,'lora_display_added_mutable_bytes':lora_display_added,'lora_display_mutable_by_unit':lora_display_mutable,'lora_passive_fast':r.get('lora_passive_fast',False),'lora_passive_slow':slow,'executable_end_exclusive':executable_end,'hardware':'NOT_TESTED','deployment_authorized':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out',type=Path,help='Existing build output directory')
    args=parser.parse_args()
    with contextlib.redirect_stdout(sys.stderr):
        result=inspect(args.out)
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    main()
