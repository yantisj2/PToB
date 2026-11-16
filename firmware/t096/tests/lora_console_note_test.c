/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual optional-note producer with synthetic console/time/radio boundaries.
 * This is observer hygiene coverage, not a model of the physical USB outage. */
#include "lora_ram_fixture.h"

static void sample(unsigned ms){
 /* A note poll must not alter the fault journal or radio/source state. */
 unsigned char old_owner[sizeof owner],old_role[sizeof role];
 memcpy(old_owner,&owner,sizeof owner);memcpy(old_role,&role,sizeof role);
 struct lp_diagnostics old_diagnostics=diagnostics;
 struct r2_lora_view old_published=published;
 union r2_diag_scratch old_scratch=r2_diag_scratch;
 atomic_val_t old_flags=atomic_get(&flags),old_fault=atomic_get(&faulted);
 unsigned old_txs=txs,old_rearms=rearms,old_stops=stops;
 now_us=(uint64_t)ms*1000;r2_lora_console_poll(22,ms);
 assert(!memcmp(old_owner,&owner,sizeof owner)&&!memcmp(old_role,&role,sizeof role));
 assert(!memcmp(&old_diagnostics,&diagnostics,sizeof diagnostics));
 assert(!memcmp(&old_published,&published,sizeof published));
 assert(!memcmp(&old_scratch,&r2_diag_scratch,sizeof old_scratch));
 assert(old_flags==atomic_get(&flags)&&old_fault==atomic_get(&faulted));
 assert(old_txs==txs&&old_rearms==rearms&&old_stops==stops);
}

int main(int argc,char **argv){
 bool tx=argc==1||atoi(argv[1])!=0;
 init(tx);step();atomic_set(&flags,ALLOWED|REQUESTED|SOURCE_PRESENT);
 published=(struct r2_lora_view){.requested=true,.active=true,.transmitter=tx,
  .time_valid=true,.state=RC8_AVAILABLE,.sampled_us=now_us,.until_us=UINT64_MAX};
 diagnostics.stream=3;diagnostics.source_epoch=5;
 console_reader=false;unsigned locks=lock_calls,formats=console_formats;
 for(unsigned ms=1000;ms<=60000;ms+=125)sample(ms);
 if(console_formats!=formats||lock_calls!=locks||console_attempts){
  fprintf(stderr,"closed-console work detected: snapshots=%u locks=%u formats=%u attempts=%u\n",
   console_snapshots,lock_calls-locks,console_formats-formats,console_attempts);
  return 86; /* The runner requires this specific old-body counterexample. */
 }
 assert(console_snapshots==60&&console_notes==0);

 /* Closed-reader context changes remain current; no stale note is queued.
  * The pending terminal fault is never cleared or rewritten by this path. */
 diagnostics.stream=42;diagnostics.source_epoch=17;diagnostics.error=-EIO;
 published.fault=true;r2_diag_scratch.lora.record[0]=R2_LORA_FAULT_MAGIC;
 r2_diag_scratch.lora.record[1]=(uint32_t)-EIO;r2_diag_scratch.lora.terminal=1;
 console_reader=true;sample(61000);
 assert(console_notes==1&&console_attempts==1&&console_formats==formats+1);
 assert(strstr(console_note,tx?"role=TX state=FAULT":"role=RX state=FAULT")&&strstr(console_note,"stream=42 epoch=17")&&strstr(console_note,"error=-5"));
 for(unsigned ms=61125;ms<62000;ms+=125)sample(ms);
 assert(console_notes==1&&console_snapshots==61); /* Existing 1Hz sampling. */

 /* Priority replies may refuse a note. A later reader gets the latest
  * context, without a backlog or a change to fault/airtime/radio state. */
 console_busy=true;diagnostics.source_epoch=18;sample(62000);
 assert(console_notes==1&&console_attempts==2);
 console_reader=false;locks=lock_calls;formats=console_formats;
 for(unsigned ms=63000;ms<=67000;ms+=125)sample(ms);
 assert(console_attempts==2&&lock_calls==locks&&console_formats==formats);
 console_reader=true;console_busy=false;sample(68000);
 assert(console_notes==2&&strstr(console_note,"epoch=18")&&strstr(console_note,"state=FAULT"));

 /* A cached true DTR may race a close. Final queue admission still refuses
  * that one formatted note; reopening retries the undelivered signature. */
 diagnostics.source_epoch=19;console_drop_after_snapshot=true;sample(69000);
 assert(!console_reader&&console_notes==2&&console_attempts==4);
 console_reader=true;sample(70000);
 assert(console_notes==3&&strstr(console_note,"epoch=19"));

 /* Ordinary packet bookkeeping does not break the minute cadence. */
 published.fault=false;published.state=RC8_AVAILABLE;published.time_valid=true;
 published.choice=RC8_SELECT_LORA;
 diagnostics.error=0;if(tx)role.tx.context_ready=true;published.sampled_us=71000000;
 published.age_ms=0;diagnostics.last_us=71000000;
 diagnostics.stream=900;diagnostics.source_epoch=901;source_events[3]=7;
 sample(71000);assert(console_notes==4&&strstr(console_note,tx?"state=ACTIVE":"state=RX_ACTIVE"));
 unsigned attempts=console_attempts,notes=console_notes;
 for(unsigned ms=72000;ms<131000;ms+=1000){
  ++diagnostics.sent[3];++diagnostics.cursor;++diagnostics.serial;
  diagnostics.last_us=(uint64_t)ms*1000;diagnostics.charged+=100;
  published.sampled_us=(uint64_t)ms*1000;published.age_ms=0;sample(ms);
 }
 assert(console_attempts==attempts&&console_notes==notes);
 sample(131000);assert(console_attempts==attempts+1&&console_notes==notes+1);
 printf("PASS cached closed-reader skips LoRa snapshot/formatting; %s reopen latest/fault preservation/priority refusal/DTR race/minute cadence\n",tx?"TX":"RX");
 return 0;
}
