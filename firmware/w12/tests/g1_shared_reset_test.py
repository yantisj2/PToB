#!/usr/bin/env python3
"""Compile the actual shared reset callbacks with isolated owner-state fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile

APP=Path(__file__).resolve().parents[1]
def body(file,signature):
    text=(APP/'main'/file).read_text();start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

transport=body('native_transport.cpp','void reset(int reason)')
diagnostic=body('native_diagnostic_tx.cpp','void diagnostic_tx_reset()')
direct=body('native_direct.cpp','void direct_host_reset()')
source=r'''
#include <cassert>
#include <cstdint>
#define V12_R2_ENABLED 1
#define V12_R2_CONFIG 13
namespace transport {
bool ready=true;unsigned resets=0,r2_calls=0,other_calls=0;
struct Lease {unsigned generation=0;} lease;
struct Client {bool requested=false,scan=false,pending=false,synced=false,fault=false;void fail(){fault=true;}} client;
void publish(){} void inc(unsigned& n){++n;}
void direct_host_reset(){++other_calls;} void diagnostic_tx_reset(){++other_calls;}
void r2_host_reset(int reason){assert(reason==19);++r2_calls;}
''' + transport + r'''
}
namespace diagnostic {
struct Lease {unsigned generation=0;} lease;
bool pending=false;
struct Owned {bool requested=false,periodic=false,discovery=false,fault=false;} owned;
void publish(){}
''' + diagnostic + r'''
}
namespace direct {
constexpr unsigned NoHandle=65535;
enum class DirectStage {Off,Acquire,Fault};
struct Host {unsigned incarnation=0,handle=NoHandle;DirectStage stage=DirectStage::Off;bool encrypted=false,confirmed=false;} host;
bool request_pending=false,stop_pending=false,abort_pending=false;
enum class JobKind {Reset};struct Job {JobKind kind;unsigned incarnation;};unsigned jobs=0;
void queue_job(const Job& j){assert(j.kind==JobKind::Reset&&j.incarnation==host.incarnation);++jobs;}
void publish(){}
''' + direct + r'''
}
int main(){
  transport::reset(19);assert(!transport::ready&&!transport::client.fault&&transport::r2_calls==1&&transport::other_calls==2);
  for(unsigned flag=0;flag<5;++flag){transport::client={};transport::lease={};
    if(flag==0)transport::lease.generation=1;
    if(flag==1)transport::client.requested=true;if(flag==2)transport::client.scan=true;
    if(flag==3)transport::client.pending=true;if(flag==4)transport::client.synced=true;
    transport::reset(19);assert(transport::client.fault);
  }
  transport::client={};transport::client.fault=true;transport::lease={};transport::reset(19);assert(transport::client.fault);
  diagnostic::diagnostic_tx_reset();assert(!diagnostic::owned.fault);
  for(unsigned flag=0;flag<5;++flag){diagnostic::owned={};diagnostic::lease={};diagnostic::pending=false;
    if(flag==0)diagnostic::lease.generation=1;if(flag==1)diagnostic::pending=true;
    if(flag==2)diagnostic::owned.requested=true;if(flag==3)diagnostic::owned.periodic=true;if(flag==4)diagnostic::owned.discovery=true;
    diagnostic::diagnostic_tx_reset();assert(diagnostic::owned.fault&&!diagnostic::owned.requested&&!diagnostic::owned.periodic&&!diagnostic::owned.discovery);
  }
  diagnostic::owned={};diagnostic::owned.fault=true;diagnostic::lease={};diagnostic::pending=false;
  diagnostic::diagnostic_tx_reset();assert(diagnostic::owned.fault);
  direct::direct_host_reset();assert(direct::jobs==0&&direct::host.stage==direct::DirectStage::Off);
  for(unsigned flag=0;flag<6;++flag){direct::host={};direct::request_pending=direct::stop_pending=direct::abort_pending=false;
    if(flag==0)direct::host.incarnation=1;if(flag==1)direct::host.stage=direct::DirectStage::Acquire;
    if(flag==2)direct::host.handle=12;if(flag==3)direct::request_pending=true;
    if(flag==4)direct::stop_pending=true;if(flag==5)direct::abort_pending=true;
    const auto before=direct::jobs;direct::direct_host_reset();assert(direct::jobs==before+1&&direct::host.stage==direct::DirectStage::Fault&&direct::host.handle==direct::NoHandle&&!direct::host.encrypted&&!direct::host.confirmed);
  }
}
'''
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp);(path/'test.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',path/'test.cpp','-o',path/'test'],check=True)
    subprocess.run([path/'test'],check=True)
print('PASS actual shared reset callbacks: unused, admitted, pending and sticky failure owners')
