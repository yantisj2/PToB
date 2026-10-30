#!/usr/bin/env python3
"""V3 whole-response parser; import has no USB/RF/reset side effects.

A matching USB arrival is not freshness. Only an exact requested nonce, bound
boot/source/build, complete CRC and original device deadline form a response.
Sticky historical loss is retained; finite capture loss uses an unchanged base.
"""
import re
import hashlib
import json
import secrets
import zlib
ROWS=('W12_V12_STATUS','W12_V12_GPS','W12_V12_LORA_STATUS','W12_V12_PUBLIC','W12_V12_CONSOLE','W12_V12_DIRECT','W12_V12_DIAGNOSTIC_TX','W12_V12_COMMAND')
COUNTERS={'dropped':'base_dropped','truncated':'base_truncated','isr_refused':'base_isr','write_errors':'base_errors','expired':'base_expired','frame_drops':'base_frames'}
HEX16=re.compile(r'^[0-9a-f]{16}$')

def require(ok,reason):
    if not ok:raise RuntimeError(reason)

def fields(line):
    parts=line.split()
    out={}
    for item in parts[1:]:
        require('=' in item,'Malformed response field')
        key,value=item.split('=',1)
        require(key not in out and key and value,'Duplicate/empty response field')
        out[key]=value
    return out

def validate_provisional_profile(mesh,manifest):
    profile=manifest.get('provisional_path_profile')
    value=mesh.get('pcfg')
    if profile is not None:
        keys={'schema','scope','offset_ns','root','root_boot','source_epoch','receiver',
              'receiver_source_sha256','receiver_phase_correction_ns','evidence_sha256',
              'baseline_evidence_sha256','publisher'}
        require(isinstance(profile,dict) and keys<=set(profile),
                'Incomplete manifest provisional provenance')
        identity={key:profile[key] for key in keys}
        if 'allow_source_epoch_transfer' in profile:
            require(type(profile['allow_source_epoch_transfer'])is bool,'Invalid epoch-transfer opt-in')
            identity['allow_source_epoch_transfer']=profile['allow_source_epoch_transfer']
        digest=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        require(profile.get('provenance_sha256')==digest and profile.get('id')==digest[:16] and
                type(profile.get('offset_ns'))is int and -100000<=profile['offset_ns']<=100000 and
                profile['offset_ns']!=0 and profile['publisher']==manifest.get('r2_client_identity'),
                'Invalid manifest provisional profile identity')
        # Status carries a compact prefix within the unchanged whole-frame
        # bound; the nonce-bound source/build and full manifest bind the rest.
        require(value==profile['id'][:8],
                'Configured aggregate-path profile differs from exact manifest')
    else:
        require(value in (None,'00000000'),
                'Unreviewed configured aggregate-path offset')


def fresh_nonce():
    while True:
        value=secrets.token_hex(8)
        if int(value,16):return value

def status_command(nonce):
    require(HEX16.fullmatch(nonce) and int(nonce,16),'Invalid nonce')
    return ('v12 status '+nonce+'\n').encode()

def mutation_command(frame,nonce,action):
    require(HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh mutation nonce required')
    holdover=re.fullmatch(r'holdover test (0|[1-9][0-9]{3,5})',action)
    bounded_holdover=bool(holdover) and (int(holdover[1])==0 or 1000<=int(holdover[1])<=120000)
    require(bounded_holdover or action in ('off','prepare_update','r2 client','r2 client 50','r2 client 100','capture stop','watchdog-test','upstream auto','upstream relay','lora auto','lora on','lora off','ui page 0','ui page 9','ui page 14','ui page 15','rc9 diag') or re.fullmatch(r'((rc|gm|rc9)-parent (auto|[0-9a-f]{16})|capture rolling [0-9]{5,6}|capture [0-9]{1,5}|tx (root|relay|rc7probe) [0-9]{1,5}|rx (full|half350|half400|half450|coarse) [0-9]{1,5}|lora_rx [0-9]{1,5}|r2 (passive|active) [0-9]{1,5})',action),'Known bounded observer/radio or explicit persistent client action required')
    b=frame['begin']
    return f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()

class Parser:
    def __init__(self,nonce,manifest,session=None,capture_base=None,allow_uninitialized_g1=False):
        require(HEX16.fullmatch(nonce) and int(nonce,16),'Invalid expected nonce')
        self.nonce,self.manifest,self.session,self.capture_base=nonce,manifest,session,capture_base
        self.allow_uninitialized_g1=allow_uninitialized_g1
        self.expected_rows=('W12_R2_STATUS',*ROWS) if manifest.get('r2_profile') in ('r2-control','r2-p1','r2-a5','r2-a5-16','r2-g1') else ROWS
        self.frame_bytes=manifest.get('console_frame_bytes',8192 if len(self.expected_rows)==9 else 6144)
        require(self.frame_bytes in (6144,8192,8704),'Unreviewed console frame capacity')
        if self.frame_bytes==8704:require(manifest.get('fault_watchdog')=='SUSTAINED_FAULT_120S_V1','Extended frame requires exact watchdog image contract')
        self.buffer=b'';self.begin=None;self.raw=b'';self.rows={};self.ignored=0;self.total=0
    def feed(self,data):
        self.total+=len(data);require(self.total<=256*1024,'Finite framed response input cap')
        self.buffer+=data;require(len(self.buffer)<=32768,'Finite incomplete response cap')
        results=[]
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            if not line:continue
            if line.startswith(b'W12_V12_BEGIN '):
                text=line.decode('ascii');b=fields(text)
                if b.get('nonce')!=self.nonce:self.ignored+=1;self.begin=None;self.raw=b'';self.rows={};continue
                require(self.begin is None,'Duplicate matching BEGIN/incomplete response')
                require(b.get('version')=='3' and HEX16.fullmatch(b.get('ticket','')) and int(b['ticket'],16)>0,'Version/ticket missing')
                for key,expected in [('factory',self.manifest['factory_mac']),('source',self.manifest['source_commit']),('build',self.manifest['build_id'])]:
                    require(b.get(key)==expected,'Response '+key+' mismatch')
                boot=b.get('session','');require(HEX16.fullmatch(boot) and int(boot,16) and (self.session is None or boot==self.session),'Reset/boot session mismatch')
                require(int(b['deadline_us'])-int(b['request_us'])==1500000 and int(b['ticket_deadline_us'])-int(b['request_us'])==2000000,'Original response/ticket deadline mismatch')
                self.begin=b;self.raw=b'\n'+line+b'\n';self.rows={};continue
            if self.begin is None:self.ignored+=1;continue
            text=line.decode('ascii');kind=text.split(' ',1)[0]
            if kind=='W12_V12_END':
                require(len(self.raw)+len(line)+1<=self.frame_bytes,'Whole-response capacity exceeded')
                e=fields(text);b=self.begin
                require(e.get('version')=='3' and e.get('nonce')==self.nonce and e.get('ticket')==b['ticket'],'END request binding mismatch')
                require(e.get('rows')==str(len(self.expected_rows)) and tuple(self.rows)==self.expected_rows,'Missing/duplicate/out-of-order response rows')
                require(e.get('crc32')==f'{zlib.crc32(self.raw)&0xffffffff:08x}','Response body CRC mismatch')
                require(line.endswith((' end_crc32='+e.get('end_crc32','')).encode()),'Canonical END CRC field missing')
                end_body=line.rsplit(b' end_crc32=',1)[0]
                require(e.get('end_crc32')==f'{zlib.crc32(end_body,zlib.crc32(self.raw))&0xffffffff:08x}','Whole-response END/counter CRC mismatch')
                require(e.get('request_us')==b['request_us'] and e.get('deadline_us')==b['deadline_us'] and int(b['request_us'])<=int(e['completed_us'])<int(e['deadline_us']),'Late/inconsistent original response completion')
                require(e.get('body_driver_drained')=='1' and e.get('counter_exhausted')=='0','Driver body drain/counter scope unproven')
                for key in ('capture_id','capture_active','capture_deadline_us',*COUNTERS,*COUNTERS.values(),*('req_'+k for k in COUNTERS)):require(key in e,'Missing capture/coverage field '+key)
                require(HEX16.fullmatch(e['capture_id']) and e['capture_active'] in ('0','1'),'Capture identity/active missing')
                delta={k:int(e[k])-int(e[base]) for k,base in COUNTERS.items()}
                require(all(v>=0 for v in delta.values()),'Counters cleared/wrapped or baseline changed')
                request_delta={k:int(e[k])-int(e['req_'+k]) for k in COUNTERS}
                require(all(v>=0 for v in request_delta.values()),'Fresh request counters cleared/wrapped')
                base={'capture_id':e['capture_id'],'capture_deadline_us':e['capture_deadline_us'],**{k:e[k] for k in COUNTERS.values()}}
                if self.capture_base is not None:require(base==self.capture_base,'Original finite capture base/deadline changed')
                # Historical nonzero counters remain explicit findings. Capture
                # coverage is usable only when its mandatory current delta is0.
                c=self.rows['W12_V12_COMMAND'];require(c.get('nonce')==self.nonce,'Command row nonce mismatch')
                status=self.rows['W12_V12_STATUS']
                if len(self.expected_rows)==9:
                    mesh=self.rows['W12_R2_STATUS']
                    if self.manifest.get('version') in ('0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34'):require(self.manifest.get('holdover_horizon_contract')==('SAMPLED_PREFIX_Q99_1US_WITNESS_V2' if self.manifest['version'] in ('0.2.30','0.2.31','0.2.32','0.2.33','0.2.34') else 'SAMPLED_PREFIX_Q99_1US_V1'),'Sampled hold-duration UI contract missing')
                    if self.manifest.get('version') in ('0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34'):
                        rate_only=self.manifest.get('version') in ('0.2.26','0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34')
                        require(self.manifest.get('holdover_servo_contract')==('BLE_RATE_CONSENSUS_TEMPERATURE_SHADOW_V3' if self.manifest.get('version') in ('0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34') else 'BLE_RATE_RETAINED_TEMPERATURE_SHADOW_V2' if rate_only else 'BLE_THERMAL_RETAINED_PRODUCTION_V1'),'Production holdover model/readout mismatch')
                        if rate_only:require(self.manifest.get('holdover_temperature_policy')=='SHADOW_ONLY','Temperature shadow policy missing')
                    require(mesh.get('profile')==self.manifest['r2_profile'] and mesh.get('enrollment_sha')==self.manifest['r2_enrollment_sha256'] and mesh.get('self')=='3','R2 compiled profile/enrollment/self mismatch')
                    if self.manifest['r2_profile'] in ('r2-a5-16','r2-g1'):
                        not_started=self.allow_uninitialized_g1 and self.manifest['r2_profile']=='r2-g1' and status.get('host_ready')=='0' and all(mesh.get(k)=='0' for k in ('requested','client_running','host_stack_configured_bytes','precision_interval_units','precision_hz')) and mesh.get('schedule_descriptor')=='0000000000000000'
                        require(not_started or (mesh.get('precision_interval_units')=='50' and mesh.get('precision_hz')=='16' and mesh.get('schedule_descriptor')==self.manifest.get('r2_fixed_schedule_hex')=='503132020000f424'),'Compiled16Hz cadence/schedule binding mismatch')
                    if self.manifest['r2_profile']=='r2-g1':
                        validate_provisional_profile(mesh,self.manifest)
                        require(mesh.get('client_capabilities')=='0' and mesh.get('client_control')=='0' and mesh.get('serving')=='0' and mesh.get('source')=='0','G1 client-only permission violated')
                        require((not_started and mesh.get('group_id')==mesh.get('client_identity')=='0000000000000000' and mesh.get('key_epoch')=='0') or (mesh.get('group_id')==self.manifest['r2_group_id'] and mesh.get('key_epoch')==str(self.manifest['r2_key_epoch']) and mesh.get('client_identity')==self.manifest['r2_client_identity']),'G1 group/factory identity mismatch')
                    if self.manifest.get('r2_g1_connection_owner_revision')==3:
                        require(mesh.get('g1_connection_owner_revision')=='3' and mesh.get('g1_sdk_connect_pending') in ('0','1'),'G1 deferred-connect diagnostics version missing')
                        for key in ('g1_pending_disconnects','g1_pending_link_teardowns','g1_cancel_calls','g1_last_disconnect_at_us'):
                            require(re.fullmatch(r'[0-9]+',mesh.get(key,'')) is not None,'Missing G1 connection diagnostic '+key)
                        for key in ('g1_cancel_rc','g1_last_disconnect_reason'):
                            require(re.fullmatch(r'-?[0-9]+',mesh.get(key,'')) is not None,'Missing G1 connection result '+key)
                    if self.manifest.get('r2_g1_discovery_diagnostic_revision')==1:
                        require(mesh.get('g1_discovery_diagnostic_revision')=='1' and mesh.get('g1_sdk_scan_active') in ('0','1'),'G1 discovery diagnostics version missing')
                        compact=self.manifest.get('w12_lora_status_contract')=='W12_RC8_LR2021_COMPACT_DISCOVERY_V1'
                        if compact:require(self.manifest.get('version') in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34') and self.manifest.get('lora_rx') is True,'Compact discovery requires exact W12 LoRa image contract')
                        omitted=('g1_adv_incomplete','g1_adv_long','g1_adv_nonrandom','g1_discovery_checked','g1_group_records','g1_last_adv_at_us','g1_last_group_at_us')
                        if compact:require(all(key not in mesh for key in omitted),'Unexpected mixed compact discovery layout')
                        for key in ('g1_adv_callbacks','g1_last_key_epoch',*(() if compact else omitted)):
                            require(re.fullmatch(r'[0-9]+',mesh.get(key,'')) is not None,'Missing G1 discovery diagnostic '+key)
                        require(re.fullmatch(r'[0-9a-f]{16}',mesh.get('g1_last_group_id','')) is not None,'Invalid G1 observed group')
                    if self.manifest.get('parent_selector_revision') in (2,3):
                        if self.manifest.get('parent_selector_revision')==3:
                            require(self.manifest.get('parent_selection_policy')=='G1_SERVER_HISTORY_SRAM_V3','Unknown compact SRAM parent-selection policy')
                            require(re.fullmatch(r'[0-9]+/-?[0-9]+/-?[0-9]+/[0-9]+',mesh.get('sel7','')) is not None,'Compact server history status missing')
                            entries,current,best,reason=map(int,mesh['sel7'].split('/'))
                            require(0<=entries<=16 and (current==-32768 or -512<=current<=512) and (best==-32768 or -512<=best<=512) and 0<=reason<=15,'Compact server history status out of bounds')
                        require(re.fullmatch(r'[0-9a-f]{16}/[0-9]+/[0-4]/[0-9]+/[0-9]+/[01]',mesh.get('rp','')) is not None,'Typed parent selector status missing')
                        target,generation,state,deadline,completed,relay=mesh['rp'].split('/')
                        excluded=self.manifest.get('rc7_exclude_direct_gm',False)
                        if self.manifest.get('relay_ui_policy')=='BLE_RELAY_UI_V1':
                            command=self.rows['W12_V12_COMMAND']
                            require(re.fullmatch(r'[01]/[01]/[01]',command.get('um','')) is not None,'Runtime upstream mode status missing')
                            applied,requested,pending=command['um'].split('/')
                            require(re.fullmatch(r'[01]/[01]/[0-7]',command.get('uii','')) is not None,'Runtime indicator status missing')
                            require(mesh.get('rc7','').split('/')[1:2]==[applied],'Applied upstream mode disagrees with direct exclusion')
                            # G1's compact row has no clock_valid field. Verify
                            # its actual published parent/source withdrawal;
                            # the native tests separately cover clock_valid.
                            fenced=mesh.get('following')=='0' and mesh.get('joined_peer')=='0000000000000000' and mesh.get('root_identity')=='0000000000000000' and mesh.get('root_boot')=='0000000000000000' and all(mesh.get(k)=='0' for k in ('source_epoch','publication','service_until_us')) and mesh.get('rc7','').split('/')[2:4]==['0','0']
                            require(pending=='0' or fenced,'Pending upstream mode did not withdraw published parent/source')
                            excluded=applied=='1'
                        if self.manifest.get('relay_path_diagnostic')=='RC7_ADMITTED_PATH_SNAPSHOT_V1':
                            path=mesh.get('path7','');require(re.fullmatch(r'[0-9a-f]{178}',path) is not None,'Bounded admitted path snapshot missing')
                            raw=bytes.fromhex(path);bound=mesh.get('rc7','').split('/')[2:4]
                            if bound[:1]==['0']:require(not any(raw),'Unbound or fenced reader retained a path')
                            else:
                                depth=raw[4];epoch=int.from_bytes(raw[:4],'little');require(epoch>0 and depth in (1,2) and bound==['1',str(depth)],'Admitted path depth/source epoch mismatch')
                                nodes=[];previous_slot=0
                                for i in range(3):
                                    n=raw[5+28*i:5+28*(i+1)];identity=int.from_bytes(n[:8],'little');boot=int.from_bytes(n[8:16],'little')
                                    if i>depth:require(not any(n),'Noncanonical unused path node');continue
                                    train=int.from_bytes(n[16:20],'little');nepoch=int.from_bytes(n[20:24],'little');slot=n[24]
                                    require(identity and boot and train and nepoch and not any(n[26:]) and n[25]==int(i>0),'Invalid path node')
                                    require((i==0 and slot==0) or (i>0 and previous_slot<slot<=3),'Invalid ancestor slot ordering');previous_slot=slot
                                    require(identity!=int(self.manifest['r2_client_identity'],16) and identity not in [x[0] for x in nodes],'Circular or self-containing admitted route');nodes.append((identity,boot))
                                require(nodes[-1]==(int(mesh.get('joined_peer','0'),16),int(mesh.get('joined_boot','0'),16)),'Path does not end at admitted parent')
                                if mesh.get('following')=='1':require(nodes[0]==(int(mesh.get('root_identity','0'),16),int(mesh.get('root_boot','0'),16)) and epoch==int(mesh.get('source_epoch','0')),'Path does not lead to current authenticated root')
                                if depth==2 and mesh.get('rs7'):require(not(int(mesh['rs7'].split('/')[0],16)&16),'Terminal RC2 client advertised READY beyond maximum depth')
                        require(not(int(target,16) and relay=='0' and excluded),'GM pin bypassed upstream exclusion')
                    if self.manifest.get('r2_rx_capture')=='PERIODIC_RX_SHADOW_V1':
                        require(mesh.get('rx_shadow_version')=='1' and mesh.get('event_capture')=='SHADOW_ONLY' and mesh.get('precision')=='UNAVAILABLE','Periodic RX shadow scope mismatch')
                        for key in ('rx_installed','rx_armed','rx_fault','rx_calls','rx_candidates','rx_matched','rx_missed','rx_ambiguous','rx_overflow','rx_epoch','rx_peak','rx_hook_max_cycles','discipline_accepted','discipline_refused','rms_ns','hold_p99_ms','hold_p99_samples'):
                            require(re.fullmatch(r'[0-9]+',mesh.get(key,'')) is not None,'Missing/invalid periodic RX diagnostic '+key)
                        require(mesh['discipline_accepted']=='0' and mesh['hold_p99_ms']=='0','Unqualified shadow capture fed precision')
                    if self.manifest.get('version') in ('0.2.8','0.2.9','0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34'):
                        offsets=self.manifest.get('phase_offsets')
                        require(isinstance(offsets,dict) and offsets.get('receive_ns')==7671 and offsets.get('relay_residual_ns')==(4026 if self.manifest.get('version') in ('0.2.10','0.2.11','0.2.12','0.2.13','0.2.14','0.2.15','0.2.16','0.2.17','0.2.18','0.2.19','0.2.20','0.2.21','0.2.22','0.2.23','0.2.24','0.2.25','0.2.26','0.2.27','0.2.28','0.2.29','0.2.30','0.2.31','0.2.32','0.2.33','0.2.34') else 3030),'Missing phase offset disclosure')
                        require(mesh.get('rxcal') in ('0','7671'),'Missing/invalid delivered receive phase')
                    if self.manifest.get('r2_rx_reference')=='CONTROLLER_1M_REF_DIRECT_BRACKET_V1':
                        require(mesh.get('cal') in ('0/0','3056/1'),'Missing/invalid experimental output correction status')
                        if mesh['cal']=='3056/1':require(self.manifest.get('output_correction_ns')==3056 and mesh['listening_percent']=='100','Untested output experiment outside reviewed BLE100 scope')
                    if self.manifest.get('r2_link_diagnostics_version') in (1,2):
                        require(mesh.get('link_diagnostics_version')==str(self.manifest['r2_link_diagnostics_version']),'Compiled link diagnostic version mismatch')
                        for key in ('peer_losses','setup_retries','last_api','last_api_rc','last_api_peer','last_api_at_us','link_mask','gatt_mask'):
                            require(re.fullmatch(r'-?[0-9]+',mesh.get(key,'')) is not None,'Missing/invalid link diagnostic '+key)
                        require(0<=int(mesh['link_mask'])<=15 and 0<=int(mesh['gatt_mask'])<=15,'Link diagnostic mask exceeds roster')
                        if self.manifest['r2_link_diagnostics_version']==2:
                            for key in ('first_error_api','first_error_rc','first_error_at_us','scan_retry_deadline_us','adv_retry_deadline_us'):
                                require(re.fullmatch(r'-?[0-9]+',mesh.get(key,'')) is not None,'Missing GAP diagnostic '+key)
                for key in ('factory','source','build','session'):require(status.get(key)==b[key],'Status row '+key+' mismatch')
                frame={'begin':b,'rows':self.rows.copy(),'end':e,'capture_base':base,'capture_delta':delta,'request_delta':request_delta,
                  'historical_loss':{k:int(e[k]) for k in COUNTERS},'ignored_queued_lines':self.ignored,
                  'freshness':'REQUEST_NONCE_COMPLETE_CRC_BOOT_SOURCE_BUILD_ORIGINAL_DEADLINE','physical_RF_time':'UNKNOWN'}
                frame['host_snapshot_initialized']=self.rows.get('W12_R2_STATUS',{}).get('host_stack_configured_bytes')!='0'
                results.append(frame);self.begin=None;self.raw=b'';self.rows={};continue
            require(kind in self.expected_rows and kind not in self.rows,'Interleaved/duplicate/unknown control row')
            self.rows[kind]=fields(text);self.raw+=line+b'\n'
            require(len(self.raw)<=self.frame_bytes,'Response body capacity exceeded')
        return results
    def complete(self):require(self.begin is None and not self.buffer,'Incomplete response retained; no completed proof')

def require_coverage(frame,capture_id=None):
    require(all(v==0 for v in frame['request_delta'].values()),'Current fresh response console coverage incomplete')
    if capture_id is not None:
        require(frame['end']['capture_id']==capture_id,'Expected capture id missing')
        require(all(v==0 for v in frame['capture_delta'].values()),'Current finite capture console coverage incomplete')
    return frame

def require_off(frame, recovered_history=False):
    r=frame['rows']
    if 'W12_R2_STATUS' in r:
        expected={'mode':'0','requested':'0','scan':'0','advertising':'0','sync_pending':'0','synced':'0','connecting':'0','connections':'0','owner_busy':'0','off_ack':'1','fault':'0','api_errors':'0','serving':'0','following':'0','precision':'UNAVAILABLE','WiFi':'OFF','PTS':'OFF'}
        if recovered_history:
            mesh=r['W12_R2_STATUS'];rearms=int(mesh.get('g1_rearm','0'))
            # Continuous BLE has no half-duty quiet boundary. A supported
            # SyncCancel timeout contributes one API error in100, versus the
            # timeout plus retained quiet failure in50. Keep exact ledgers;
            # this exception never admits other faults or renews a drain.
            percent=mesh.get('listening_percent','50')
            require(percent in ('50','100'),'Exact recovered listening profile required')
            quiet_per_rearm=1 if percent=='50' else 0
            require(mesh.get('profile')=='r2-g1' and rearms>0 and
              mesh.get('first_error_api')=='13' and mesh.get('first_error_rc')=='19' and
              int(mesh.get('first_error_at_us','0'))>0 and
              int(mesh.get('api_errors','0'))==(1+quiet_per_rearm)*rearms and int(mesh.get('quiet_faults','0'))==quiet_per_rearm*rearms and
              int(mesh.get('client_generation','0'))==rearms+1,
              'Exact retained recovered G1 history required')
            expected['api_errors']=mesh['api_errors']
        for key,value in expected.items():require(r['W12_R2_STATUS'].get(key)==value,'Fresh R2 OFF/fault gate/'+key)
        mesh=r['W12_R2_STATUS'];require(mesh.get('stop_generation')==mesh.get('stop_completed') and mesh.get('deadline_exceeded')=='0','R2 original owner stop/deadline gate')
        if 'client_generation' in mesh:
            require(all(mesh.get(k)=='0' for k in ('client_pending','client_running','client_stopping','client_deadline_exceeded')) and mesh.get('client_stop_generation')==mesh.get('client_stop_completed'),'Persistent client actual drain gate')
            require(mesh['client_generation']=='0' or int(mesh.get('client_completed_us','0'))>0,'Persistent client completion missing')
    return require_aux_off(frame)

def require_aux_off(frame,r2_shared_host=False):
    """Every non-R2 owner must remain OFF, including in an active field client."""
    r=frame['rows']
    rules={'W12_V12_STATUS':{'requested':'0','scan':'0','pending':'0','sync':'0','host_busy':'0','command_pending':'0','off_ack':'1','lifecycle':'0','host_ready':'1','api_errors':'0','lease_faults':'0','WiFi':'OFF','PTS':'OFF','authority':'NONE','precision':'UNAVAILABLE'},
      'W12_V12_DIAGNOSTIC_TX':{'requested':'0','periodic':'0','discovery':'0','pending':'0','owner_busy':'0','off_ack':'1','fault':'0','api_errors':'0'},
      'W12_V12_LORA_STATUS':{'enabled':'0','requested':'0','worker_busy':'0','off_ack':'1','failed':'0','errors':'0','TX':'OFF'},
      'W12_V12_DIRECT':{'stage':'0','api_errors':'0','crypto_errors':'0','queue_drops':'0'},
      'W12_V12_PUBLIC':{'queue_drops':'0'}}
    if r2_shared_host:
        # HostWork covers the one shared NimBLE task, including the active R2
        # owner. Its busy/OFF ledger cannot be idle during that owner's HCI.
        # Prove the legacy RX owner never admitted service instead; all other
        # RF owner and fault checks remain exact.
        rules['W12_V12_STATUS'].pop('host_busy');rules['W12_V12_STATUS'].pop('off_ack')
        rules['W12_V12_STATUS'].update(rx_generation='0',rx_id='0000000000000000',rx_admitted_us='0',rx_deadline_us='0',rx_started_us='0')
    for kind,expected in rules.items():
        for key,value in expected.items():require(r[kind].get(key)==value,'Fresh OFF/fault gate '+kind+'/'+key)
    require(r['W12_V12_STATUS'].get('rx_stop_generation')==r['W12_V12_STATUS'].get('rx_stop_completed') and
            r['W12_V12_DIAGNOSTIC_TX'].get('stop_generation')==r['W12_V12_DIAGNOSTIC_TX'].get('stop_completed') and
            r['W12_V12_LORA_STATUS'].get('generation')==r['W12_V12_LORA_STATUS'].get('acknowledged_generation'),'Owner stop generation not completed')
    return frame
