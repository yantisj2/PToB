/* MIT. Copyright (c) 2026 Jonathan Yantis. Pinned UATT cleanup boundary. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cleanup_budget.h"
#include "uc_capacity_under_test.h"
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 19
#define K_MSEC(n) ((n)*1000)
#define K_FOREVER 0
struct bt_conn {unsigned refs,disconnects;};
struct uc_client {struct bt_conn *conn;bool dead,submitting,tx_busy,tx_done,command_pending,probe_pending,state_pending;struct {unsigned hz;} session;};
static struct {struct uc_client clients[UC1_CLIENTS];bool enabled,advertising;void *adv;} u;
static struct {bool closing;uint64_t at,close_deadline;} status;
static uint64_t now,dead_at[UC1_CLIENTS],submit_until,busy_until;
static int lock,job,state_lock,disconnect_result,adv_result;
static unsigned polls;
typedef int k_spinlock_key_t;
static int k_spin_lock(int *p){(void)p;return 0;}
static void k_spin_unlock(int *p,int k){(void)p;(void)k;}
static void k_mutex_lock(int *p,int t){(void)p;(void)t;}
static void k_mutex_unlock(int *p){(void)p;}
static uint64_t now_us(void){return now;}
static uint64_t mono_us(void){return now;}
static struct bt_conn *bt_conn_ref(struct bt_conn *c){++c->refs;return c;}
static void bt_conn_unref(struct bt_conn *c){assert(c->refs);--c->refs;}
static int bt_conn_disconnect(struct bt_conn *c,unsigned reason){assert(reason==19);++c->disconnects;return disconnect_result;}
static int bt_le_ext_adv_stop(void *p){assert(p);return adv_result;}
static int bt_le_ext_adv_delete(void *p){assert(p);return adv_result;}
static int k_work_cancel(int *p){(void)p;return 0;}
static int k_work_busy_get(int *p){(void)p;return now<busy_until;}
static void k_sleep(uint64_t us);
#include "uc_cleanup_under_test.h"
static void k_sleep(uint64_t us){
 now+=us;++polls;
 assert(!u.enabled&&!u.advertising);
 for(unsigned i=0;i<UC1_CLIENTS;i++){
  struct uc_client *c=&u.clients[i];
  assert(!c->command_pending&&!c->probe_pending&&!c->state_pending);
  if(c->conn&&dead_at[i]&&now>=dead_at[i])disconnected(c->conn,19);
  if(now>=submit_until)c->submitting=false;
 }
}
static struct bt_conn conn[UC1_CLIENTS];
static void reset(void){
 memset(&u,0,sizeof u);memset(conn,0,sizeof conn);memset(dead_at,0,sizeof dead_at);
 now=1000000;polls=0;submit_until=busy_until=0;disconnect_result=adv_result=0;
 status.closing=true;status.at=0;status.close_deadline=now+R2_CLEANUP_US;
 u.enabled=u.advertising=true;u.adv=(void*)1;
 for(unsigned i=0;i<2;i++){conn[i].refs=2;u.clients[i]=(struct uc_client){.conn=&conn[i],.tx_busy=true,.command_pending=true,.probe_pending=true,.state_pending=true,.session={16}};}
}
int main(void){
 reset();uint64_t until=status.close_deadline;
 dead_at[0]=now+31000000;dead_at[1]=now+39000000;
 assert(r2_uc_close_begin());assert(!u.enabled&&!u.adv);
 for(unsigned i=0;i<2;i++)assert(conn[i].disconnects==1&&conn[i].refs==2);
 /* Other link cleanup consumes 31 seconds; UCS requests already ran. */
 now+=31000000;disconnected(&conn[0],19);
 assert(r2_uc_close_until(until,cleanup_progress));
 assert(now==40000000&&status.at==now&&status.close_deadline==41000000);
 assert(r2_uc_quiescent());for(unsigned i=0;i<2;i++)assert(conn[i].refs==1&&conn[i].disconnects==1);
 reset();until=status.close_deadline;
 for(unsigned i=0;i<UC1_CLIENTS;i++){
  conn[i].refs=2;u.clients[i].conn=&conn[i];dead_at[i]=now+31000000+i*1000000;
 }
 assert(r2_uc_close_begin());assert(r2_uc_close_until(until,cleanup_progress));
 for(unsigned i=0;i<UC1_CLIENTS;i++)assert(conn[i].refs==1&&conn[i].disconnects==1);
 reset();until=now+25000000;assert(r2_uc_close_begin());
 assert(!r2_uc_close_until(until,cleanup_progress));assert(now==until);
 uint64_t at=status.at;now+=1000000;assert(status.at==at); /* No fabricated stalled progress. */
 for(unsigned i=0;i<2;i++)assert(conn[i].refs==2&&conn[i].disconnects==1);
 assert(!r2_uc_close_until(until,cleanup_progress));assert(now==until+1000000); /* Expired wait never renews. */
 reset();until=now+5000000;dead_at[0]=dead_at[1]=now+1000000;
 u.clients[0].submitting=true;submit_until=now+3000000;busy_until=now+4000000;
 assert(r2_uc_close_begin());assert(r2_uc_close_until(until,cleanup_progress));
 assert(now==5000000&&conn[0].refs==1); /* Both submission and work must retire. */
 reset();sent(&conn[0],NULL);assert(!u.clients[0].tx_busy&&u.clients[0].tx_done);
 u.clients[1].submitting=true;sent(&conn[1],NULL);assert(u.clients[1].tx_busy&&u.clients[1].tx_done);
 for(unsigned boundary=0;boundary<3;boundary++){
  reset();until=now+R2_CLEANUP_US;assert(r2_uc_close_begin());
  now=until-1+boundary;disconnected(&conn[0],19);disconnected(&conn[1],19);
  assert(r2_uc_close_until(until,cleanup_progress)==(boundary==0));
  assert(conn[0].refs==1&&conn[1].refs==1); /* Safe release never excuses late closure. */
 }
 reset();status.closing=false;status.at=7;cleanup_progress();assert(status.at==7);
 disconnect_result=-EIO;assert(!r2_uc_close_begin());assert(conn[0].refs==2&&!r2_uc_quiescent());
 puts("PASS actual UCS begin/wait: concurrent original deadline, delayed callbacks, no authority, retained refs, submission/work guards, stale-poll visibility");
}
