#!/usr/bin/env python3
"""Actual auxiliary admission/retirement bodies, explicit owner-state fakes.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
def body(file,signature):
 s=(W12/'main'/file).read_text();start=s.index(signature);brace=s.index('{',start);i=brace+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
code='''#include <cassert>
#include <cstring>
#include "native_transport.hpp"
#include "native_diagnostic_tx.hpp"
#include "native_direct.hpp"
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
bool gate=false;unsigned reservations=0;
namespace v12 {bool transport_watch_ble_admit_begin(bool){if(!gate)return false;++reservations;return true;}void transport_watch_ble_admit_end(){assert(reservations);--reservations;}}
namespace v12::periodic_tx {struct Probe{bool installed=false,armed=false;};Probe probe;Probe status(){return probe;}}
namespace diag {
using namespace v12;int tx_mux=0;bool pending=false,stop_pending=false;uint16_t next_bytes=0;OwnerLease lease;DiagnosticTxStatus owned,snapshot;
uint64_t micros(){return 1000;}unsigned publishes=0;void publish(){++publishes;}
TransportStatus transport_status(){assert(reservations);TransportStatus s;s.host_ready=true;return s;}DirectStatus direct_status(){return {};}
'''
code+=body('native_diagnostic_tx.cpp','bool diagnostic_tx_request(')+'\n'+body('native_diagnostic_tx.cpp','bool diagnostic_tx_watch_retire()')+'\n}\n'
code+='''namespace direct_test {
using namespace v12;int direct_mux=0;constexpr uint16_t NoHandle=UINT16_MAX;
struct Addr{uint8_t type=0,val[6]{};};
struct Host{DirectStage stage=DirectStage::Off;LinkRole role=LinkRole::None;uint32_t incarnation=0;uint16_t handle=NoHandle;bool encrypted=false,confirmed=false;Addr peer;uint64_t root_permit=0,lease_end=0;}host,requested;
struct Queue{unsigned count=0;}jobs,transmit;
bool credential_present=true,request_pending=false,stop_pending=false,abort_pending=false;DirectStatus snapshot;
uint64_t micros(){return 1000;}unsigned publishes=0;void publish(){++publishes;}void count(uint32_t DirectStatus::*field){++(snapshot.*field);}
'''
code+=body('native_direct.cpp','bool direct_request(')+'\n'+body('native_direct.cpp','bool direct_watch_retire_unused()')+'\n}\n'
code+='''int main(){
 uint8_t address[6]={1,2,3,4,5,6};
 assert(!diag::diagnostic_tx_request(v12::RootBytes,1000,1)&&!diag::pending&&!reservations);
 assert(!direct_test::direct_request(v12::LinkRole::GM,1,address,0,1000)&&!direct_test::request_pending&&!reservations);
 gate=true;assert(diag::diagnostic_tx_request(v12::RootBytes,1000,1)&&diag::pending&&!reservations);
 assert(!diag::diagnostic_tx_watch_retire());diag::pending=false;diag::lease.pending=false;diag::stop_pending=true;
 diag::lease.stop_generation=7;diag::lease.deadline_us=123;diag::owned.api_errors=9;diag::owned.fault=true;
 v12::periodic_tx::probe.installed=true;assert(!diag::diagnostic_tx_watch_retire()&&diag::stop_pending);
 v12::periodic_tx::probe.installed=false;assert(diag::diagnostic_tx_watch_retire()&&!diag::stop_pending&&diag::lease.stop_completed==7&&diag::lease.deadline_us==123&&diag::owned.fault&&diag::owned.api_errors==9);
 assert(direct_test::direct_request(v12::LinkRole::GM,1,address,0,1000)&&direct_test::request_pending&&!reservations);
 assert(!direct_test::direct_watch_retire_unused());direct_test::request_pending=false;direct_test::stop_pending=direct_test::abort_pending=true;
 direct_test::host.incarnation=1;assert(!direct_test::direct_watch_retire_unused());direct_test::host.incarnation=0;
 direct_test::jobs.count=1;assert(!direct_test::direct_watch_retire_unused());direct_test::jobs.count=0;
 direct_test::host.stage=v12::DirectStage::Fault;direct_test::snapshot.api_errors=13;
 assert(direct_test::direct_watch_retire_unused()&&!direct_test::stop_pending&&!direct_test::abort_pending&&direct_test::host.stage==v12::DirectStage::Off&&direct_test::snapshot.api_errors==13);
 gate=false;assert(!diag::diagnostic_tx_request(v12::RootBytes,1000,2)&&!reservations);
}
'''
(out/'test.cpp').write_text(code);cc='/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'
cmd=[cc,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DPTOB_TWATCH=1','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(q)for q in (W12/'main',W12/'tests/console_fakes')],str(out/'test.cpp'),'-o',str(out/'test')]
for name,command in [('compile',cmd),('runtime',[str(out/'test')])]:
 with (out/(name+'.log')).open('w') as f:subprocess.run(command,check=True,stdout=f,stderr=subprocess.STDOUT,timeout=60)
files=[W12/'main'/q for q in ('native_direct.cpp','native_direct.hpp','native_diagnostic_tx.cpp','native_diagnostic_tx.hpp','native_transport.hpp')]+[Path(__file__)]
(out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'ACTUAL_AUXILIARY_FUNCTION_BODIES_OWNER_FAKES_ASAN_UBSAN','hardware_access':False,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}},indent=2)+'\n');print('PASS actual auxiliary bodies: lifecycle admission reservation, no pending-work erasure, retired stop-only quarantine, active capture/ACL refusal, historical errors/deadlines retained')
