/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Single-thread dispatcher-test backend boundary. Optional observer helpers
 * expose no RF clock and reuse the fixture's existing UI/status producers.
 * This is not a service-lock, precision or try-read qualification fixture. */
#include "rf_clock.h"
static bool profile_observer_before(r2_ui_read_boundary_fn boundary,void *context,unsigned owner)
{return !boundary||boundary(context,owner,false);}
static bool profile_observer_after(r2_ui_read_boundary_fn boundary,void *context,unsigned owner)
{return !boundary||boundary(context,owner,true);}
void r2_normal_service_ui_logic(struct r2_ui_model *m,const struct r2_service_status *s)
{(void)s;r2_normal_service_ui(m);}
void r2_dynamic_service_ui_logic(struct r2_ui_model *m,const struct r2_service_status *s)
{(void)s;r2_dynamic_service_ui(m);}
void r2_normal_service_ui_details(struct r2_ui_model *m,const struct r2_service_status *s)
{(void)m;(void)s;}
void r2_dynamic_service_ui_details(struct r2_ui_model *m,const struct r2_service_status *s)
{(void)m;(void)s;}
bool r2_normal_service_ui_lock_probe(void){return true;}
bool r2_dynamic_service_ui_lock_probe(void){return true;}
bool r2_normal_service_snapshot_try(struct r2_service_status *out,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!out||!profile_observer_before(boundary,context,R2_UI_READ_GM))return false;
 r2_normal_service_snapshot(out);return profile_observer_after(boundary,context,R2_UI_READ_GM);
}
bool r2_dynamic_service_snapshot_try(struct r2_service_status *out,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!out||!profile_observer_before(boundary,context,R2_UI_READ_RC))return false;
 r2_dynamic_service_snapshot(out);return profile_observer_after(boundary,context,R2_UI_READ_RC);
}
bool r2_normal_service_ui_details_try(struct r2_ui_model *m,const struct r2_service_status *s,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!profile_observer_before(boundary,context,R2_UI_READ_GM))return false;
 r2_normal_service_ui_details(m,s);return profile_observer_after(boundary,context,R2_UI_READ_GM);
}
bool r2_dynamic_service_ui_details_try(struct r2_ui_model *m,const struct r2_service_status *s,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!profile_observer_before(boundary,context,R2_UI_READ_RC))return false;
 r2_dynamic_service_ui_details(m,s);return profile_observer_after(boundary,context,R2_UI_READ_RC);
}
void r2_normal_service_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now)
{(void)at;(void)now;if(out)*out=(struct r2_rf_clock_sample){0};}
void r2_dynamic_service_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now)
{(void)at;(void)now;if(out)*out=(struct r2_rf_clock_sample){0};}
bool r2_normal_service_rf_clock_try(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!out||!profile_observer_before(boundary,context,R2_UI_READ_GM))return false;
 r2_normal_service_rf_clock(out,at,now);return profile_observer_after(boundary,context,R2_UI_READ_GM);
}
bool r2_dynamic_service_rf_clock_try(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now,r2_ui_read_boundary_fn boundary,void *context)
{
 if(!out||!profile_observer_before(boundary,context,R2_UI_READ_RC))return false;
 r2_dynamic_service_rf_clock(out,at,now);return profile_observer_after(boundary,context,R2_UI_READ_RC);
}
