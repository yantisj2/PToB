/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "initiator_diag.h"
#define CONFIG_BT_CENTRAL 1
#define BT_HCI_LE_ADV_PROP_CONN 1
#define PDU_ADV_TYPE_AUX_CONNECT_RSP 8
#define PDU_AC_EXT_HEADER_SIZE_MIN 1
#define ADVA_SIZE 6
#define TARGETA_SIZE 6
#define ADVA_OFFSET 0
#define TGTA_OFFSET 6
#define BDADDR_SIZE 6
#define EVENT_IFS_US 150
#define EVENT_OVERHEAD_START_US 40
#define EVENT_TICKER_RES_MARGIN_US 16
#define SCAN_INT_UNIT_US 625
#define HAL_TICKER_TICKS_TO_US(x) (x)
#define PDU_AC_MAX_US(n,phy) ((n)*8+40)
#define HDR_LLL2ULL(p) (&(p)->ull)
#define unlikely(x) (x)
#define LL_ASSERT(x) assert(x)
#define IS_ENABLED(x) 0
#define FILTER_IDX_NONE 255
#define lll_prof_latency_capture() ((void)0)
#define radio_ar_has_match() 0
#define radio_ar_match_get() 0
#define radio_phy_flags_rx_get() 0
#define lll_isr_rx_status_reset() ((void)0)
struct pdu_adv_connect_ind {uint8_t init_addr[6],data[28];};
struct pdu_adv_ext_hdr {uint8_t adv_addr,tgt_addr,data[12];};
struct pdu_adv_com_ext_adv {uint8_t mode,ext_hdr_adv_data;};
struct pdu_adv {uint8_t type,len,tx_addr,rx_addr;struct {uint8_t adv_mode;struct pdu_adv_ext_hdr ext_hdr;} adv_ext_ind;struct pdu_adv_connect_ind connect_ind;};
struct node_rx_pdu {uint8_t pdu[sizeof(struct pdu_adv)];};
struct node_rx_ftr {int unused;};
struct lll_scan_aux {struct node_rx_pdu *node_conn_rx;};
struct lll_conn {struct {bool cancelled;} central;};
struct lll_prepare_param {unsigned unused;};
struct event_done_extra {unsigned unused;};
#define EVENT_DONE_EXTRA_TYPE_SCAN_AUX 1
struct ull_hdr {uint32_t ticks_slot;};
struct lll_scan {struct lll_conn *conn;struct lll_scan_aux *lll_aux;struct ull_hdr ull;uint32_t ticks_window,interval;uint8_t init_addr_type,init_addr[6],adv_addr_type,adv_addr[6];};
static unsigned available=4,prepared,tx_set,tx_isr;
static uint32_t end_us;
static bool target=true;
static bool done,crc;
static struct lll_scan *response_parent;
static struct node_rx_pdu rx;
static struct pdu_adv scratch;
static bool lll_scan_ext_tgta_check(struct lll_scan *l,bool a,bool b,struct pdu_adv *p,unsigned r,void *v){return target;}
static struct node_rx_pdu *ull_pdu_rx_alloc_peek(unsigned n){return available>=n?&rx:NULL;}
static uint32_t radio_tmr_end_get(void){return end_us;}
static struct pdu_adv *radio_pkt_scratch_get(void){return &scratch;}
static void radio_pkt_tx_set(void *p){assert(p==&scratch);++tx_set;}
static void lll_scan_prepare_connect_req(struct lll_scan *l,struct pdu_adv *p,uint8_t phy,uint8_t flags,uint8_t type,uint8_t *address,uint8_t own_type,uint8_t *own,uint32_t *space){
 assert(phy==1&&address&&own);++prepared;*space=1000;
}
static bool parent_is_scan=true;
static struct lll_scan *ull_scan_aux_lll_parent_get(void *a,void *b){if(b)*(uint8_t *)b=parent_is_scan;return response_parent;}
static bool radio_is_done(void){return done;}
static bool radio_crc_is_valid(void){return crc;}
static void isr_rx_connect_rsp(void *p);
static void isr_tx(void *aux,void *pdu,void(*handler)(void*),void *param){assert(aux&&pdu&&handler==isr_rx_connect_rsp&&param==aux);++tx_isr;}
static bool lll_scan_adva_check(struct lll_scan *l,uint8_t type,const uint8_t *a,uint8_t rl){return type==l->adv_addr_type&&!memcmp(a,l->adv_addr,6);}
static unsigned disabled,completed;
static void isr_done(void *p){assert(p);}
static void radio_isr_set(void(*handler)(void*),void *p){assert(handler==isr_done&&p);}
static void radio_disable(void){++disabled;}
static int lll_hfclock_off(void){return 0;}
static struct event_done_extra *ull_done_extra_type_set(unsigned type){static struct event_done_extra e;assert(type==1);return &e;}
static void lll_done(void *p){++completed;}
#include "aux_under_test.h"
static unsigned count(unsigned c){struct r2_init_diag s;r2_init_diag_snapshot(&s);return s.count[c];}
static int admission(struct lll_scan *l,struct lll_scan_aux *a,struct pdu_adv *p){return isr_rx_pdu(l,a,&rx,p,1,0,0,0,0,0,255,0);}
int main(void){
 struct lll_conn conn={0};struct lll_scan_aux aux={0};
 struct lll_scan l={.conn=&conn,.ull={.ticks_slot=10000},.ticks_window=10000,.interval=80,.adv_addr_type=1,.adv_addr={1,2,3,4,5,0xc6}};
 struct pdu_adv p={.adv_ext_ind={.adv_mode=1}};
 target=false;assert(admission(&l,&aux,&p)==-EINVAL&&!count(R2_INIT_MATCH));target=true;
 conn.central.cancelled=true;assert(admission(&l,&aux,&p)==-EINVAL);conn.central.cancelled=false;
 p.adv_ext_ind.adv_mode=0;assert(admission(&l,&aux,&p)==-EINVAL);p.adv_ext_ind.adv_mode=1;
 assert(admission(&l,NULL,&p)==-ECHILD&&count(R2_INIT_MATCH)==1&&count(R2_INIT_AUX_NO_CONTEXT)==1);
 available=3;assert(admission(&l,&aux,&p)==-ENOBUFS&&count(R2_INIT_BUFFER)==1);
 struct r2_init_diag s;r2_init_diag_snapshot(&s);assert(s.rx_needed==4&&s.rx_free==3);
 available=4;end_us=10000;assert(admission(&l,&aux,&p)==-ETIME&&count(R2_INIT_WINDOW)==1&&!prepared);
 end_us=1000;assert(!admission(&l,&aux,&p)&&prepared==1&&tx_set==1&&count(R2_INIT_REQUEST)==1);
 // The inherited LLL auxiliary context is equally valid; count a production
 // request for both scheduling paths, with no changed gate or return value.
 l.lll_aux=&aux;assert(!admission(&l,NULL,&p)&&prepared==2&&count(R2_INIT_REQUEST)==2);
 isr_tx_connect_req(&aux);assert(tx_isr==1);
 // Execute actual production AUX response validation, including both address
 // directions and canonical mode/length. Wrong RF responses never succeed.
 struct pdu_adv sent={.tx_addr=1,.connect_ind={.init_addr={6,5,4,3,2,0xc1}}};
 struct pdu_adv response={.type=PDU_ADV_TYPE_AUX_CONNECT_RSP,.rx_addr=1,.tx_addr=1,
  .len=offsetof(struct pdu_adv_com_ext_adv,ext_hdr_adv_data)+offsetof(struct pdu_adv_ext_hdr,data)+ADVA_SIZE+TARGETA_SIZE,
  .adv_ext_ind={.ext_hdr={.adv_addr=1,.tgt_addr=1}}};
 memcpy(response.adv_ext_ind.ext_hdr.data,l.adv_addr,6);memcpy(response.adv_ext_ind.ext_hdr.data+6,sent.connect_ind.init_addr,6);
 assert(isr_rx_connect_rsp_check(&l,&sent,&response,255));
 struct pdu_adv good=response;
 for(unsigned i=0;i<8;i++){
  response=good;
  if(i==0)response.type=0;if(i==1)--response.len;if(i==2)response.adv_ext_ind.adv_mode=1;
  if(i==3)response.adv_ext_ind.ext_hdr.adv_addr=0;if(i==4)response.adv_ext_ind.ext_hdr.tgt_addr=0;
  if(i==5)response.tx_addr=0;if(i==6)response.adv_ext_ind.ext_hdr.data[0]^=1;
  if(i==7)response.adv_ext_ind.ext_hdr.data[6]^=1;
  assert(!isr_rx_connect_rsp_check(&l,&sent,&response,255));
 }
 // Execute the production response ISR through its checked classification,
 // including raw DONE/CRC before the checker normalizes trx_done.
 response_parent=&l;scratch=sent;
 for(unsigned i=0;i<4;i++){
  done=i!=0;crc=i!=1;response=good;if(i==2)response.rx_addr=0;
  memcpy(rx.pdu,&response,sizeof response);aux.node_conn_rx=&rx;
  isr_rx_connect_rsp(&aux);assert(!aux.node_conn_rx);
 }
 for(unsigned i=R2_INIT_AUX_RSP_OK;i<R2_INIT_COUNTERS;i++)assert(count(i)==1);
 // The actual abort callback observes active initiating scans only, while
 // preserving both native cancellation cleanup branches and null safety.
 unsigned aborted=count(R2_INIT_ABORT);struct lll_prepare_param prep={0};
 parent_is_scan=false;abort_cb(&prep,&aux);assert(count(R2_INIT_ABORT)==aborted);
 parent_is_scan=true;conn.central.cancelled=true;abort_cb(&prep,&aux);assert(count(R2_INIT_ABORT)==aborted);
 conn.central.cancelled=false;abort_cb(&prep,&aux);assert(count(R2_INIT_ABORT)==aborted+1);
 abort_cb(NULL,&aux);assert(count(R2_INIT_ABORT)==aborted+2&&disabled==1);
 abort_cb(&prep,NULL);assert(count(R2_INIT_ABORT)==aborted+2&&completed==4);
 puts("PASS pinned production AUX: target/cancel/mode, missing context,4buffer refusal, slot deadline, request/TX ISR, response addresses/canonical checks and DONE/CRC/invalid/accepted categories; no RF");
}
