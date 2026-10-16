#include <assert.h>
#include <stdio.h>
struct k_timer{int unused;};
static unsigned supervise_requested,wakes;
static void k_sem_give(unsigned *sem){assert(sem==&supervise_requested);if(!*sem)*sem=1;++wakes;}
#include "supervisor_timer_under_test.h"
int main(void){struct k_timer timer={0};for(unsigned i=0;i<100;i++)supervise_timer(&timer);assert(wakes==100&&supervise_requested==1);supervise_requested=0;supervise_timer(&timer);assert(supervise_requested==1);puts("actual timer ISR only signals coalesced semaphore, no mutex or service call PASS");}
