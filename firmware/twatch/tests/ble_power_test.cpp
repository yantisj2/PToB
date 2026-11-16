// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ble_power.hpp"
#include <cassert>
#include <cstdio>
using watch::BlePower;
static BlePower active(){BlePower p;p.controller=2;p.host_running=true;p.synced(100);assert(p.stage==BlePower::Active);return p;}
int main(){
 auto p=active();assert(!p.off_ack());assert(p.request(false,1000));
 assert(p.stage==BlePower::Draining&&!p.requested&&p.deadline_us==2001000);
 const auto deadline=p.deadline_us;
 assert(p.request(true,2000)&&p.request(false,3000)&&p.request(true,4000));
 assert(p.deadline_us==deadline&&p.stage==BlePower::Draining); // No toggle cancels or extends drain.
 p.stage=BlePower::HostStop;assert(!p.off_ack());p.stage=BlePower::HostExit;assert(!p.off_ack());
 p.stopped(5000);assert(p.stage==BlePower::Init&&p.deadline_us==5005000&&!p.off_ack());
 p.stage=BlePower::Sync;p.controller=2;p.host_running=true;p.synced(6000);assert(p.stage==BlePower::Active&&p.requested);
 assert(p.request(false,7000));p.stopped(8000);assert(p.stage==BlePower::Off&&p.off_ack());
 p.host_running=true;assert(!p.off_ack());p.host_running=false;p.controller=1;assert(!p.off_ack());p.controller=0;
 assert(p.request(true,9000)&&p.stage==BlePower::Init);assert(p.request(false,10000)&&p.stage==BlePower::Draining);
 p.stage=BlePower::Sync;p.synced(11000);assert(p.stage==BlePower::Draining&&p.deadline_us==2010000);
 p.stopped(12000);assert(p.off_ack());
 // Timeout is sticky: a late SDK success cannot report OFF or restart.
 p=active();assert(p.request(false,1000));p.stage=BlePower::Deinit;p.observe(2001000);
 assert(p.stage==BlePower::Fault&&p.failed_stage==BlePower::Deinit&&p.error==-110);
 p.stopped(2001001);assert(p.stage==BlePower::Fault&&!p.off_ack());assert(!p.request(true,2001002));
 p=active();assert(p.request(false,1000));p.fail(-5);p.observe(3000000);assert(p.error==-5&&p.failed_stage==BlePower::Draining);
 p=active();p.generation=UINT32_MAX;assert(!p.request(false,1000)&&p.stage==BlePower::Fault);
 p=active();assert(!p.request(false,UINT64_MAX-1)&&p.stage==BlePower::Fault);
 p=active();assert(p.request(false,1000));p.stopped(2000);assert(p.request(true,3000));
 p.stage=BlePower::Sync;p.synced(5003000);assert(p.stage==BlePower::Fault); // No late ON acknowledgement.
 // One separate bounded cleanup of an ON failure retains the first capsule.
 p=active();p.stage=BlePower::Init;p.fail(-12,10000);const auto first=p.error;const auto generation=p.fault_generation;
 assert(p.operating_fault&&p.begin_cleanup(11000));const auto cleanup_deadline=p.cleanup_deadline_us;
 assert(cleanup_deadline==2011000&&p.request(true,12000)&&p.cleanup_deadline_us==cleanup_deadline);
 p.stopped(13000);assert(p.off_ack()&&p.operating_fault&&p.error==first);
 const auto closed=p.generation;assert(p.request(false,14000)&&p.generation==closed&&p.off_generation==closed);
 assert(!p.recover(15000));assert(p.request(true,16000)&&!p.operating_fault&&p.stage==BlePower::Init);
 p.fail(-5,17000);assert(p.error==first&&p.fault_generation==generation&&p.last_error==-5&&p.last_fault_generation>generation);
 assert(p.begin_cleanup(18000));p.stage=BlePower::Deinit;p.observe(p.cleanup_deadline_us);
 assert(p.cleanup_error==-110&&p.error==first&&!p.off_ack()&&!p.begin_cleanup(p.cleanup_deadline_us+1));
 puts("PASS BLE power lifecycle: original deadlines through rapid toggles, actual OFF predicate, startup cancellation, sticky timeout/SDKfailure, exhausted generations/time overflow; synthetic only");
}
