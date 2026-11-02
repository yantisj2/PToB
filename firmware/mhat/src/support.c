/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * The leaf reuses the current RC owner. These display/observer adapters grant
 * no permission and never substitute for the owner's authenticated clock.
 */
#include "ble_service.h"
#include "rc_dynamic_service.h"
#include "rc_shadow.h"
#include "health.h"
#include "time_service.h"
#include <string.h>
bool r2_switch_rc_active(void){return true;}
void r2_service_stop(void){r2_dynamic_service_stop();}
void r2_service_snapshot(struct r2_service_status *s){r2_dynamic_service_snapshot(s);}
void r2_health_lock_enter(unsigned site,unsigned mask){(void)site;(void)mask;}
void r2_health_lock_leave(unsigned mask){(void)mask;}
void r2_shadow_normal_withdraw(void){}
void r2_shadow_serving(bool enabled){(void)enabled;}
void r2_shadow_poll(void){}
void r2_shadow_map(const struct rc5_clock *c,const struct rc5_state *s,uint64_t tick,uint64_t epoch){(void)c;(void)s;(void)tick;(void)epoch;}
void r2_ble_timing_lamps(bool tx,bool correction,uint64_t now){(void)tx;(void)correction;(void)now;}
void r2_ble_lamps(uint8_t out[8],uint64_t now){(void)now;memset(out,0,8);}
void r2_time_precision_snapshot(struct r2_pps_precision_view *out){memset(out,0,sizeof *out);}
int r2_console_note(const char *bytes,size_t size,uint64_t expiry){(void)bytes;(void)size;(void)expiry;return -EAGAIN;}
