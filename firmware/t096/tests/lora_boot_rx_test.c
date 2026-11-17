/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "lora_ram_fixture.h"
int main(int argc,char **argv){
 assert(argc==2);bool designated=atoi(argv[1])!=0;init(designated);
#ifdef R2_LORA_BOOT_RX
 assert(!transmitter);
#elif defined(R2_LORA_BOOT_TX) && R2_LORA_BOOT_TX
 assert(transmitter);
#else
 assert(transmitter==designated);
#endif
 assert(atomic_get(&flags)&REQUESTED);
 if(transmitter){
  // Boot role grants no RF without an admitted source; RC still rejects TX.
  unsigned sent_before=txs;now_us+=10000000;
  for(unsigned n=0;n<100;n++)step();assert(txs==sent_before);
  r2_lora_control(true,true,RC8_SELECT_NONE);
  assert(r2_lora_mode_request(true)==-EACCES);
  for(unsigned n=0;n<100;n++)step();assert(txs==sent_before);
  r2_lora_control(false,true,RC8_SELECT_NONE);
 }
 if(!transmitter){
  r2_lora_control(true,true,RC8_SELECT_NONE);
  assert(r2_lora_mode_request(true)==-EACCES&&!transmitter);
  r2_lora_control(false,true,RC8_SELECT_NONE);
  assert(!r2_lora_mode_request(true));
  for(int n=0;n<100&&!transmitter;n++)step();
  assert(transmitter&&(atomic_get(&flags)&REQUESTED));
 }
 assert(!atomic_get(&faulted));
#if defined(R2_LORA_BOOT_TX) && R2_LORA_BOOT_TX
 r2_lora_init(2,22,1,NULL);assert(!transmitter);
 r2_lora_init(0,22,1,&test_group);assert(!transmitter);
#endif
 puts("PASS boot role, requested RX, GM manual TX and RC rejection");return 0;
}
