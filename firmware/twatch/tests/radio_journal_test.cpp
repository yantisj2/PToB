// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
uint64_t fake_now=1;void (*fake_worker)(void*)=nullptr;
std::map<std::string,std::vector<unsigned char>> disk,pending;
int storage_rc=0;unsigned iterations=0;bool pause_in_commit=false;
#define V12_BUILD_ID "host-test"
#include "../main/fault_watchdog.cpp"
int nvs_open(const char*,int,int* h){*h=1;return storage_rc;}
int nvs_get_blob(int,const char* key,void* out,size_t* n){auto i=disk.find(key);if(i==disk.end()||*n!=i->second.size())return -1;std::memcpy(out,i->second.data(),*n);return 0;}
int nvs_set_blob(int,const char* key,const void* p,size_t n){const auto b=static_cast<const unsigned char*>(p);pending[key]={b,b+n};return storage_rc;}
int nvs_commit(int){if(pause_in_commit){assert(!watch::radio_journal_idle());watch::radio_journal_stop();assert(!watch::radio_journal_idle());}if(storage_rc)return storage_rc;for(auto& v:pending)disk[v.first]=v.second;pending.clear();return 0;}
void nvs_close(int){}
struct Finished{};
void vTaskDelay(unsigned){fake_now+=100000;if(!--iterations)throw Finished{};}
void pump(unsigned n){iterations=n;try{fake_worker(nullptr);}catch(Finished&){};}
int main(){
 uint8_t factory[6]={1};assert(v12::fault_watchdog_start(factory,55));
 watch::RadioFaultRecord r;r.radio=0;r.event=1;r.rc=-42;r.site=7;r.parent=123;
 watch::radio_journal_record(r);pump(2);assert(disk.empty()); // board storage not yet ready
 watch::radio_journal_ready();pump(2);auto j=watch::radio_journal_status();
 assert(j.saved==1&&j.latest[0].rc==-42&&j.latest[0].boot==55&&disk.size()==2);
 // Recovering cannot erase the recorded fault reason.
 r.event=4;r.rc=0;watch::radio_journal_record(r);pump(2);assert(watch::radio_journal_status().latest[0].rc==-42);
 // Simulated reboot reloads committed flash; never execute an MCU reset.
 journal={};sequence=0;pump(2);assert(watch::radio_journal_status().latest[0].rc==-42);
 for(unsigned i=0;i<24;++i){r.event=2;r.rc=-43;watch::radio_journal_record(r);}pump(20);
 assert(disk.size()==9&&watch::radio_journal_status().dropped==8);
 for(unsigned i=1;i<=8;++i){auto slot=watch::radio_journal_status(i);assert(slot.slot_valid&&slot.latest[0].sequence);}
 assert(watch::radio_journal_status(9).slot_valid&&watch::radio_journal_status(9).latest[0].rc==-42);
 assert(!watch::radio_journal_status(10).slot_valid&&!watch::radio_journal_status(11).slot_valid);
 first_fault[0]={};pump(2);assert(watch::radio_journal_status(9).latest[0].rc==-42);
 storage_rc=-9;r.radio=1;r.event=1;watch::radio_journal_record(r);pump(2);
 assert(watch::radio_journal_status().storage_error==-9);
 // Loss of time is diagnostic only, no restart callback exists in this binary.
 v12::fault_watchdog_sync_progress(true,0);fake_now+=2000000000;v12::fault_watchdog_main_progress(0,0,0,0,false,true);pump(2);
 assert(!v12::fault_watchdog_status().sync_expected&&v12::fault_watchdog_status().restarts==0);
 // A retired host needs no heartbeat. Re-enabling starts a real watchdog
 // interval; a live host that subsequently stalls remains observable.
 v12::fault_watchdog_host_expected(false);fake_now+=6000000;
 v12::fault_watchdog_main_progress(0,0,0,0,false,true);pump(2);
 assert(!(v12::fault_watchdog_status().mask&v12::FaultHostStalled));
 v12::fault_watchdog_host_expected(true);assert(v12::fault_watchdog_status().host_at_us==fake_now);
 fake_now+=6000000;v12::fault_watchdog_main_progress(0,0,0,0,false,true);pump(2);
 assert(v12::fault_watchdog_status().mask&v12::FaultHostStalled);
 v12::fault_watchdog_host_expected(false);v12::fault_watchdog_main_progress(0,0,0,0,false,true);pump(2);
 assert(!(v12::fault_watchdog_status().mask&v12::FaultHostStalled));
 // Maintenance prevents all future NVS admission and exposes in-flight work.
 storage_rc=0;pause_in_commit=true;watch::radio_journal_record(r);pump(2);
 assert(watch::radio_journal_idle());const auto before=disk;watch::radio_journal_stop();
 assert(watch::radio_journal_idle());watch::radio_journal_record(r);pump(2);assert(disk==before);
}
