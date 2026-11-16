// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "maintenance_proof.hpp"
#include <cassert>
int main(){
 using v12::UpdateStage;
 const watch::MaintenanceProof complete{true,true,true,true,true,false,false,false};
 assert(complete.consumers_closed());
 for(unsigned field=0;field<5;++field){
  auto p=complete;
  bool* required[]={&p.ble_off,&p.lora_off,&p.journal_idle,&p.fine_off,&p.display_off};
  *required[field]=false;v12::UpdateGate g;assert(g.begin(100));g.poll(200,p.services());
  assert(g.stage==UpdateStage::Draining);
  g.poll(2000100,p.services());assert(g.stage==UpdateStage::Fault);
  g.poll(2000101,complete.services());assert(g.stage==UpdateStage::Fault);
  assert(!g.begin(3000000)); // No renewed deadline after a failed drain.
 }
 for(unsigned field=0;field<3;++field){
  auto p=complete;bool* failures[]={&p.ble_cleanup_fault,&p.lora_cleanup_fault,&p.fine_cleanup_fault};
  *failures[field]=true;v12::UpdateGate g;assert(g.begin(100));g.poll(200,p.services());assert(g.stage==UpdateStage::Fault);
 }
 v12::UpdateGate g;assert(g.begin(100));g.poll(1999999,complete.services());assert(g.stage==UpdateStage::Ready);
 assert(!g.begin(3000000)&&g.deadline==2000100);
 v12::UpdateGate expired;assert(expired.begin(100));expired.poll(2000100,complete.services());assert(expired.stage==UpdateStage::Fault);
 auto rendering=complete;rendering.display_off=false;
 assert(rendering.consumers_closed());assert(!rendering.services(false).host);
 auto not_retired=complete;not_retired.ble_off=false;assert(!not_retired.consumers_closed());
}
