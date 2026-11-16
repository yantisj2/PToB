/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "lora_ram_fixture.h"
int main(int argc,char **argv){
 assert(argc==2);bool designated=atoi(argv[1])!=0;init(designated);
#ifdef R2_LORA_BOOT_RX
 assert(!transmitter);
#else
 assert(transmitter==designated);
#endif
 assert(atomic_get(&flags)&REQUESTED);
 if(!transmitter){
  r2_lora_control(true,true,RC8_SELECT_NONE);
  assert(r2_lora_mode_request(true)==-EACCES&&!transmitter);
  r2_lora_control(false,true,RC8_SELECT_NONE);
  assert(!r2_lora_mode_request(true));
  for(int n=0;n<100&&!transmitter;n++)step();
  assert(transmitter&&(atomic_get(&flags)&REQUESTED));
 }
 assert(!atomic_get(&faulted));
 puts("PASS boot role, requested RX, GM manual TX and RC rejection");return 0;
}
