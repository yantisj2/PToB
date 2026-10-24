#!/usr/bin/env python3
"""Check retained GCC frames on selected main-task paths; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT

This is a bounded static regression check, not a complete call-graph proof.
The reserve covers uncounted callees (including formatting and SDK code). A
candidate still requires runtime high-water observations on its exact hardware.
"""
import argparse,json,re
from pathlib import Path
RESERVE_BYTES=4096
DECLARATIONS={
 'main':'void app_main()',
 'input':'void {anonymous}::input()',
 'command':'void {anonymous}::command(const char*)',
 'status':'void {anonymous}::status(uint64_t, uint64_t)',
 'off_ack':'bool {anonymous}::r2_command_off_ack()',
 'ui':'void {anonymous}::ui()',
 'diagnostics':'void {anonymous}::g1_task_diagnostics(uint64_t)',
 'update':'void {anonymous}::poll_update_drain(uint64_t)',
 'fault':'void {anonymous}::clock_fault_progress()',
 'authority':'bool {anonymous}::r2_authority_update_needed()',
 'aid':'void {anonymous}::aid_gnss_from_ble()',
}
PATHS={
 'status_request':('main','input','command','status'),
 'command_off_ack':('main','input','command','off_ack'),
 **{name:('main',name) for name in ('ui','diagnostics','update','fault','authority','aid')},
}
PPS_DECLARATIONS={
 'production_pps_observe':'void {anonymous}::observe_production_pps_diagnostic()',
 'production_pps_score':'void {anonymous}::score_production_pps_diagnostic(const gnss_pps::Observation&)',
 'pps_callback':'void {anonymous}::pps(const gnss_pps::Observation&)',
 'gnss_poll':'void gnss_pps::poll(uint64_t)',
}
FAST_MAP_DECLARATIONS={'gnss_fast_map':'w12_s3_gnss_map_bracket'}
def measure(config,usage,gnss_usage=None,fast_map_usage=None):
 if fast_map_usage is not None and gnss_usage is None:raise ValueError('Fast map requires retained GNSS PPS frames')
 configs=re.findall(r'^CONFIG_ESP_MAIN_TASK_STACK_SIZE=(\d+)$',config,re.M)
 if len(configs)!=1:raise ValueError('One exact main-task stack configuration required')
 configured=int(configs[0]);frames={}
 lines=[line.split('\t') for line in usage.splitlines() if line.strip()]
 optional={'rc9_diagnostics':'void {anonymous}::rc9_status_frame(uint64_t, uint64_t)'} if '::rc9_status_frame(' in usage else {}
 declarations={**DECLARATIONS,**optional,**(PPS_DECLARATIONS if gnss_usage is not None else {}),**(FAST_MAP_DECLARATIONS if fast_map_usage is not None else {})}
 if gnss_usage is not None:lines.extend(line.split('\t') for line in gnss_usage.splitlines() if line.strip())
 if fast_map_usage is not None:lines.extend(line.split('\t') for line in fast_map_usage.splitlines() if line.strip())
 for name,declaration in declarations.items():
  matches=[row for row in lines if row[0].endswith(':'+declaration)]
  if len(matches)!=1 or len(matches[0])!=3:raise ValueError('Missing or ambiguous main-task frame: '+name)
  _,size,kind=matches[0]
  if kind!='static' or not re.fullmatch(r'[1-9]\d*',size):raise ValueError('Unbounded main-task frame: '+name)
  frames[name]=int(size)
 paths={name:{'frames':list(keys),'static_bytes':sum(frames[key] for key in keys)} for name,keys in {**PATHS,**({'rc9_diagnostics':('main','input','command','rc9_diagnostics')} if optional else {})}.items()}
 if gnss_usage is not None:
  for name,keys in {'production_pps_snapshot':('main','production_pps_observe'),
                    'production_pps_callback':('main','gnss_poll','pps_callback','production_pps_score')}.items():
   paths[name]={'frames':list(keys),'static_bytes':sum(frames[key] for key in keys)}
 if fast_map_usage is not None:
  keys=('main','gnss_poll','gnss_fast_map');paths['gnss_software_map_bracket']={'frames':list(keys),'static_bytes':sum(frames[key] for key in keys)}
 maximum=max(row['static_bytes'] for row in paths.values())
 if configured<=RESERVE_BYTES or maximum+RESERVE_BYTES>configured:
  raise ValueError(f'Main-task selected path {maximum} + reserve {RESERVE_BYTES} exceeds configured {configured}')
 return {'schema':1,'status':'SELECTED_STATIC_MAIN_PATHS_WITH_RESERVE_PASS','configured_bytes':configured,
         'uncounted_callee_reserve_bytes':RESERVE_BYTES,'frames_bytes':frames,'paths':paths,
         'maximum_selected_static_bytes':maximum,'headroom_after_reserve_bytes':configured-maximum-RESERVE_BYTES,
         'scope':'SELECTED_MAIN_TASK_PATHS_NOT_COMPLETE_CALL_GRAPH','runtime_watermark':'NOT_MEASURED_BY_BUILD'}
def check(out):
 target=Path(out)/'target';files=sorted(target.rglob('native_main.cpp.su'))
 if len(files)!=1:raise ValueError('One retained native_main GCC stack-usage file required')
 gnss_usage=None;fast_map_usage=None;receipt=Path(out)/'receipt.json';device=Path(out)/'local/device.json';r=json.loads(receipt.read_text()) if receipt.exists() else {}
 if r.get('version') in ('0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8') and type(r.get('gnss_fast_map_diagnostic'))is not bool:raise ValueError('Explicit fast-map stack metadata required')
 if receipt.exists() and device.exists() and r.get('version') in ('0.1.21','0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8') and 'gnss_profile' in json.loads(device.read_text()):
  gnss=sorted(target.rglob('gnss_pps.cpp.su'))
  if len(gnss)!=1:raise ValueError('One retained GNSS PPS GCC stack-usage file required')
  gnss_usage=gnss[0].read_text()
 if r.get('version') in ('0.1.22','0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8') and r.get('gnss_fast_map_diagnostic'):
  if gnss_usage is None:raise ValueError('Fast map requires retained GNSS PPS frames')
  fast=sorted(target.rglob('s3_clock_reference.c.su'))
  if len(fast)!=1:raise ValueError('One retained fast map GCC stack-usage file required')
  fast_map_usage=fast[0].read_text()
 result=measure((target/'sdkconfig').read_text(),files[0].read_text(),gnss_usage,fast_map_usage)
 if r.get('version') in ('0.1.23','0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8') and r.get('fine_clock'):
  owners=sorted(target.rglob('w12_fine_clock.cpp.su'))
  if len(owners)!=1:raise ValueError('One retained W12 fine-owner stack unit required')
  all_rows=[line.split('\t') for f in target.rglob('*.su') for line in f.read_text().splitlines() if line.strip()]
  owner_rows=[line.split('\t') for line in owners[0].read_text().splitlines() if line.strip()]
  def frame(tail,rows=all_rows):
   matches=[x for x in rows if x[0].endswith(':'+tail)]
   if len(matches)!=1 or matches[0][2]!='static':raise ValueError('Missing/ambiguous fine static frame: '+tail)
   return int(matches[0][1])
  counter=frame('bool w12::fine::Counter::current(uint32_t, uint32_t, uint64_t, w12::fine::Time&)',owner_rows)
  current=frame('bool {anonymous}::current(w12::fine::Time&)',owner_rows)+frame('bool {anonymous}::latch(uint32_t&)',owner_rows)+counter
  pending=frame('bool {anonymous}::pending_ok()',owner_rows)+current+frame('bool w12::fine::Receiver::pending(uint64_t)',owner_rows)
  claim=frame('w12::fine::Receiver::Result w12::fine::Receiver::claim(uint64_t, uint32_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint64_t, uint32_t, Match&)',owner_rows)
  host=re.findall(r'^CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=(\d+)$',(target/'sdkconfig').read_text(),re.M)
  if len(host)!=1:raise ValueError('One retained NimBLE stack configuration required')
  f=result['frames_bytes']
  paths={
   'fine_boot':(result['configured_bytes'],f['main']+frame('bool w12_fine_start(uint64_t, uint64_t, uint64_t)')+frame('bool {anonymous}::start(uint64_t)',owner_rows)+max(current,frame('bool {anonymous}::route()',owner_rows))),
   'fine_main_current':(result['configured_bytes'],f['main']+frame('bool w12_fine_current(uint64_t*)')+pending),
   'fine_receive':(int(host[0]),frame('void v12::r2_host_poll()')+frame('void v12::{anonymous}::active_poll(uint64_t)')+frame('unsigned int v12::{anonymous}::periodic_poll(uint64_t, unsigned int)')+frame('int w12_fine_receive(v12::periodic_rx::Stamp&)')+max(claim,pending)),
   'fine_maintenance':(result['configured_bytes'],f['main']+f['update']+frame('void w12_fine_stop()'))}
  if r.get('version') in ('0.2.6','0.2.7','0.2.8') and gnss_usage is not None:
   paths['fine_gnss_map']=(result['configured_bytes'],f['main']+
    frame('void gnss_pps::poll(uint64_t)')+
    frame('bool w12_fine_gnss_map_bracket(mcpwm_cap_channel_t*, uint64_t*, uint64_t*)',owner_rows)+
    max(pending,counter,frame('bool {anonymous}::gnss_aperture(mcpwm_dev_t*, mcpwm_dev_t*, uint32_t&, uint32_t&)',owner_rows),
        frame('bool w12::fine::Receiver::coordinate(uint64_t, uint64_t, uint64_t&) const',owner_rows)))
  result['fine_paths']={}
  for name,(size,used) in paths.items():
   if used+RESERVE_BYTES>size:raise ValueError('Fine clock selected path exceeds reserved stack: '+name)
   result['fine_paths'][name]={'configured_bytes':size,'selected_static_bytes':used,'reserve_bytes':RESERVE_BYTES,'headroom_after_reserve_bytes':size-used-RESERVE_BYTES}
 if r.get('version') in ('0.2.0','0.2.1','0.2.2','0.2.3','0.2.4','0.2.5','0.2.6','0.2.7','0.2.8') and r.get('lora_rx'):
  radio=sorted(target.rglob('w12_lora_radio.cpp.su'))
  if len(radio)!=1:raise ValueError('One retained LR2021 owner frame unit required')
  rows=[line.split('\t') for q in target.rglob('*.su') for line in q.read_text().splitlines() if line.strip()]
  def rf(tail,unit=None):
   matches=[x for x in rows if x[0].endswith(':'+tail) and (not unit or '/'+unit+':' in x[0])]
   if len(matches)!=1 or matches[0][2]!='static':raise ValueError('Missing radio static frame: '+tail)
   return int(matches[0][1])
  root=rf('void {anonymous}::worker(void*)','w12_lora_radio.cpp')+rf('void {anonymous}::worker_step(bool)','w12_lora_radio.cpp')
  paths={'radio_receive':root+rf('int {anonymous}::receive()','w12_lora_radio.cpp')+rf('w12_rc8_receive')+rf('rc8_receiver_process')+rf('rc8_receiver_process_origin'),
         'radio_init':root+rf('int {anonymous}::start()','w12_lora_radio.cpp')+rf('virtual void {anonymous}::Hal::init()','w12_lora_radio.cpp')}
  result['radio_paths']={}
  for name,used in paths.items():
   if used+RESERVE_BYTES>8192:raise ValueError('Radio selected path exceeds8192byte stack: '+name)
   result['radio_paths'][name]={'configured_bytes':8192,'selected_static_bytes':used,'reserve_bytes':RESERVE_BYTES,'headroom_after_reserve_bytes':8192-used-RESERVE_BYTES}
 return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);a=p.parse_args()
 print(json.dumps(check(a.out),indent=2))
if __name__=='__main__':main()
