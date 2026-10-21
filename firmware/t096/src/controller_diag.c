/* MIT. Copyright (c) 2026 Jonathan Yantis.
 * Observation only. IRQ fences copy fixed words; no controller calls or printf.
 * Existing scan scratch tail; initialized only after startup scanner retires.
 */
#include "controller_diag.h"
#include "lora_watchdog.h"
#include "ble_service.h"
#include "build_identity.h"
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#define diag_words r2_diag_scratch.controller.words
void r2_controller_diag_init(void)
{memset(&r2_diag_scratch.controller.words,0,sizeof r2_diag_scratch.controller.words);}
void r2_controller_diag_count(unsigned word)
{
 unsigned key=irq_lock();
 if(word<4&&!diag_words[6]&&diag_words[word]!=UINT32_MAX)++diag_words[word];
 irq_unlock(key);
}
bool r2_controller_diag_failure(uintptr_t callback,uintptr_t param)
{
 unsigned key=irq_lock();
 if(diag_words[6]){irq_unlock(key);return false;}
 {diag_words[4]=(uint32_t)callback;diag_words[5]=(uint32_t)param;diag_words[6]=1;diag_words[21]=k_uptime_get_32();}
 irq_unlock(key);return true;
}
void r2_controller_diag_entry(unsigned index,uintptr_t callback,uintptr_t param,unsigned flags)
{
 if(index>=7)return;
 unsigned key=irq_lock();diag_words[7+2*index]=(uint32_t)callback;diag_words[8+2*index]=(uint32_t)param;
 diag_words[22]|=(flags&3u)<<(2*index);diag_words[6]=2+index;irq_unlock(key);
}
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS])
{unsigned key=irq_lock();memcpy(out,diag_words,R2_CONTROLLER_DIAG_WORDS*sizeof(uint32_t));irq_unlock(key);}
void r2_controller_diag_console(uint64_t boot,uint64_t now)
{
 /* Main-thread only, at most once/second; idle console refuses rather than
  * consuming command/timing queues. Counts are cumulative; gaps are explicit. */
 uint32_t *due=&r2_diag_scratch.controller.words[23];
 if((int32_t)((uint32_t)now-*due)<0)return;
 *due=(uint32_t)now+1000;
 uint32_t copy[R2_CONTROLLER_DIAG_WORDS];r2_controller_diag_snapshot(copy);
 char line[320];int n=snprintf(line,sizeof line,
  "#R2_CONTROLLER {\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"ms\":%llu,\"enqueue\":%u,\"dequeue\":%u,\"aborted_retired\":%u,\"done\":%u,\"failure\":%u}\n",
  R2_SOURCE_SHA,(unsigned long long)boot,(unsigned long long)now,copy[0],copy[1],copy[2],copy[3],copy[6]);
 if(n>0&&(size_t)n<sizeof line)(void)r2_console_note(line,(size_t)n,now+150);
}
