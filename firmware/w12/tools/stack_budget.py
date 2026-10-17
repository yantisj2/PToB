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
def measure(config,usage):
 configs=re.findall(r'^CONFIG_ESP_MAIN_TASK_STACK_SIZE=(\d+)$',config,re.M)
 if len(configs)!=1:raise ValueError('One exact main-task stack configuration required')
 configured=int(configs[0]);frames={}
 lines=[line.split('\t') for line in usage.splitlines() if line.strip()]
 for name,declaration in DECLARATIONS.items():
  matches=[row for row in lines if row[0].endswith(':'+declaration)]
  if len(matches)!=1 or len(matches[0])!=3:raise ValueError('Missing or ambiguous main-task frame: '+name)
  _,size,kind=matches[0]
  if kind!='static' or not re.fullmatch(r'[1-9]\d*',size):raise ValueError('Unbounded main-task frame: '+name)
  frames[name]=int(size)
 paths={name:{'frames':list(keys),'static_bytes':sum(frames[key] for key in keys)} for name,keys in PATHS.items()}
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
 return measure((target/'sdkconfig').read_text(),files[0].read_text())
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('out',type=Path);a=p.parse_args()
 print(json.dumps(check(a.out),indent=2))
if __name__=='__main__':main()
