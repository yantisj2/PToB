/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Drive the actual UI worker through short and long RC timing losses.
 */
#include <stdio.h>
#include "../src/ui_service.c"
int main(void){
 mode=34;struct r2_ui_model m=sync_model(0);r2_ui_publish(&m);
 r2_ui_start();assert(entry);entry(NULL,NULL,NULL);
 assert(sync_grace_frames&&sync_long_expired&&sync_recovered);
 assert(status.stopped&&!r2_ui_close_result());
 puts("PASS actual UI worker: short loss holds face, long loss becomes Syncing, live validity stays withdrawn, recovery immediate");
}
