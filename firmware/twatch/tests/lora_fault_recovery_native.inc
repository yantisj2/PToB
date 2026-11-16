// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
int main(){
 cold();assert(!n30_lora_fault_recover());
 fake::gain_bad_readback=true;force_on();worker_step(true);
 assert(failed&&owner.drained()&&published.reset_asserted&&hardware.error==-EIO);
 assert(n30_lora_off_proof()==15); // Operating error retained; physical cleanup complete.
 const auto saved_generation=published.request_generation;++published.request_generation;
 assert(n30_lora_off_proof()!=15);published.request_generation=saved_generation;
 const auto saved_ack=published.off_ack_generation;++published.off_ack_generation;
 assert(n30_lora_off_proof()!=15);published.off_ack_generation=saved_ack;
 for(unsigned bit=1;bit<4;++bit){
  bool* field=bit==1?&published.spi_retired:bit==2?&published.capture_released:&published.reset_asserted;
  *field=false;assert(n30_lora_off_proof()!=15);*field=true;
 }
 const auto fault_generation=owner.generation;const auto first_spi=hardware.spi_error;
 assert(n30_lora_fault_recover());assert(!n30_lora_fault_recover());
 fake::gain_bad_readback=false;worker_step(true);
 assert(!failed&&owned.enabled&&owner.requested&&owner.generation>fault_generation);assert(n30_lora_off_proof()!=15);
 assert(policy.mode()==n30::LoRaMode::KeepOn&&fake::regs[0x08ac]==0x96&&hardware.spi_error==first_spi);
 assert(!published.model_valid&&!published.until_us); // No previous radio grant survives.
 step(true);
 assert(!n30_lora_fault_recover());
 // CURRENT failures preserve their diagnostic capsule while new captures
 // start an independent counter epoch and require new authenticated packets.
 cold();force_on();fake::current_fail=true;worker_step(true);worker_step(true);
 const auto capsule=current_failure;const auto epoch=capture_epoch;
 fake::current_fail=false;assert(n30_lora_fault_recover());worker_step(true);
 assert(!failed&&owned.enabled&&capture_epoch==epoch+1);
 assert(current_failure.set&&current_failure.rc==capsule.rc&&current_failure.available==capsule.available&&current_failure.epoch==capsule.epoch);
 assert(!published.model_valid&&!published.until_us);
 // Partial enable is recoverable only after all actually enabled channels
 // and the timer acknowledge retirement; the new start uses a fresh epoch.
 for(int channel=1;channel<=3;channel++){
  cold();fake::fail_enable=channel;force_on();worker_step(true);
  assert(failed&&owner.drained()&&capture_ready);fake::fail_enable=0;
  assert(n30_lora_fault_recover());worker_step(true);assert(!failed&&owned.enabled&&capture_epoch==2);
 }
 for(int boundary=1;boundary<=5;boundary++){
  cold();force_on();fake::fail_disable=boundary;fault(-EIO);worker_step(true);worker_step(true);
  assert(cleanup_failed&&!n30_lora_fault_recover()&&n30_lora_off_proof()!=15);fake::fail_disable=0;worker_step(true);
  assert(owner.drained()&&cleanup_failed&&!n30_lora_fault_recover());
 }
 cold();fake::dma_fail=true;force_on();worker_step(true);
 const auto transactions=fake::starts;assert(failed&&inflight&&!n30_lora_fault_recover());
 for(unsigned i=0;i<5;i++)worker_step(true);assert(fake::starts==transactions&&inflight);
 // Fresh mailbox admission never overrides an intervening OFF/maintenance,
 // changes generation, stale worker request, or a sticky emergency.
 for(unsigned change=0;change<6;change++){
  cold();fake::gain_bad_readback=true;force_on();worker_step(true);fake::gain_bad_readback=false;
  assert(n30_lora_fault_recover());
  if(change==0)n30_lora_control(false,fake::us,false);
  if(change==1)fake::us+=250000;
  if(change==2)++owner.generation;
  if(change==3)emergency=true;
  if(change==4)assert(n30_lora_mode(n30::LoRaMode::KeepOff));
  if(change==5)capture_ready=false,cap_timer=reinterpret_cast<void*>(1);
  worker_step(true);assert(failed&&!owned.enabled&&!recover_pending);
  if(change==4){
   assert(policy.mode()==n30::LoRaMode::KeepOff&&!n30_lora_fault_recover());
   assert(n30_lora_mode(n30::LoRaMode::KeepOn));worker_step(true);
   assert(policy.mode()==n30::LoRaMode::KeepOn&&failed&&!owned.enabled);
   assert(n30_lora_fault_recover());worker_step(true);assert(!failed&&owned.enabled);
  }
 }
 cold();fake::gain_bad_readback=true;force_on();worker_step(true);fake::us+=250000;
 assert(!n30_lora_fault_recover());worker_step(true); // Fresh publication is required.
 cancel_serial=UINT32_MAX;assert(!n30_lora_fault_recover());
 cold();force_on();fake::us+=31000000;n30_lora_control(true,fake::us,true);worker_step(true);
 assert(emergency&&failed&&!n30_lora_fault_recover());
 std::puts("PASS actual watch SX1262 operating recovery, independent epochs and all physical cleanup/intent refusal guards; no device reset");
}
