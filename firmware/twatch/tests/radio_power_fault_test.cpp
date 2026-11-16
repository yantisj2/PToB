// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/radio_power_fault.hpp"
#include <cassert>
int main(){
 watch::BlePower p;p.stage=watch::BlePower::Sync;p.sync_site=3;p.fail(-12,100);
 watch::RadioFaultRecord r;watch::power_fault_record(r,p);
 assert(watch::power_fault_active(p)&&r.reason==6&&r.rc==-12&&r.boot_first_rc==-12&&r.error_at_us==100);
 assert(p.begin_cleanup(200));p.stopped(300);assert(p.off_ack());
 watch::power_fault_record(r,p);assert(r.rc==-12&&r.completed_us==300&&r.boot_first_at==100);
 assert(p.recover(400));p.sync_site=5;p.fail(-5,500);watch::power_fault_record(r,p);
 assert(r.rc==-5&&r.error_at_us==500&&r.boot_first_rc==-12&&r.boot_first_at==100&&r.site==5);
 assert(p.begin_cleanup(600));p.fail(-110,2000600);watch::power_fault_record(r,p);
 assert(r.rc==-110&&r.boot_first_rc==-12&&r.error_at_us==2000600&&r.episode==p.cleanup_generation);
 p.operating_fault=false;p.cleanup_error=0;p.stage=watch::BlePower::Active;
 assert(!watch::power_fault_active(p));
}
