/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "mhat_private.h"
#include "ble_service.h"
#include "rc_dynamic_service.h"
#include "capture.h"
#include "radio_capture.h"
#include "usb_source.h"
#include "timing_events.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/random/random.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_wdt.h>
#include <hal/nrf_power.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mhat_clock_start(uint64_t);
int mhat_pps_init(void);
void mhat_pps_poll(void);
void mhat_pps_stats(uint32_t[4]);
void mhat_pps_debug(uint32_t[5]);
int mhat_mesh_start(const struct r2_identity *);
int mhat_mesh_poll(void);
void mhat_mesh_stop(void);
bool mhat_mesh_quiescent(void);
void mhat_mesh_stats(uint32_t[6]);
int mhat_mesh_json(char *,size_t);
static const struct device *console=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct device *bus=DEVICE_DT_GET(DT_NODELABEL(uart0));
static char bus_output[8192];
static size_t bus_size,bus_at;
static atomic_t bus_busy;
static int bus_error;
static bool dfu_requested;
static int dfu_status;
static int64_t dfu_deadline;
static void bus_irq(const struct device *dev,void *unused){
    (void)unused;
    if(!uart_irq_update(dev)||!uart_irq_tx_ready(dev))return;
    int n=uart_fifo_fill(dev,(const uint8_t *)bus_output+bus_at,bus_size-bus_at);
    if(n>0)bus_at+=(size_t)n;
    if(bus_at==bus_size){uart_irq_tx_disable(dev);atomic_clear(&bus_busy);}
}
static void line(const char *s){
    /* The header UART drains in interrupts. Telemetry never waits for a host
     * or holds up the PPS planner while a complete line crosses 115200 baud. */
    size_t size=strlen(s);
    if(!bus_error&&size<sizeof bus_output&&atomic_cas(&bus_busy,0,1)){
        memcpy(bus_output,s,size);bus_at=0;bus_size=size;uart_irq_tx_enable(bus);
    }
    uint32_t dtr=0;if(!device_is_ready(console)||uart_line_ctrl_get(console,UART_LINE_CTRL_DTR,&dtr)||!dtr)return;
    for(;*s;s++)uart_poll_out(console,*s);
}
static bool array(char *out,size_t cap,size_t *at,const char *name,const uint32_t *values,size_t count){
    int n=snprintf(out+*at,cap-*at,",\"%s\":[",name);
    if(n<0||(size_t)n>=cap-*at)return false;
    *at+=(size_t)n;
    for(size_t i=0;i<count;i++){
        n=snprintf(out+*at,cap-*at,"%s%u",i?",":"",values[i]);
        if(n<0||(size_t)n>=cap-*at)return false;
        *at+=(size_t)n;
    }
    if(*at+2>=cap)return false;
    out[(*at)++]=']';out[*at]=0;return true;
}
static void watchdog(void){for(unsigned i=0;i<8;i++)if(NRF_WDT->RREN&(1u<<i))NRF_WDT->RR[i]=0x6e524635;}
static void commands(uint64_t boot){
    static char input[180];static size_t at;static bool overflow;
    unsigned char c;
    /* USB possession is the control boundary. No UART or BLE command handler.
     * Every request binds exact factory ID, full installed source and boot. */
    for(unsigned i=0;i<256&&!uart_poll_in(console,&c);i++){
        if(c=='\r')continue;
        if(c!='\n'){if(at+1<sizeof input)input[at++]=(char)c;else overflow=true;continue;}
        input[at]=0;char expected[180];
        snprintf(expected,sizeof expected,"dfu %s %s %016llx",MHAT_PARTICLE_ID,MHAT_SOURCE_SHA,(unsigned long long)boot);
        if(!overflow&&!strcmp(input,expected)&&!dfu_requested){
            dfu_requested=true;dfu_status=1;dfu_deadline=k_uptime_get()+3000;
            mhat_mesh_stop();
        }
        at=0;overflow=false;
    }
    if(dfu_status==1){
        if(mhat_mesh_quiescent()){
            r2_usb_source_withdraw();(void)r2_capture_stop();
            /* Device OS 5.8.2 platform_ram.ld: BKP_DR_01 at 0x2003f380.
             * bootloader main.c consumes ENTER_DFU_APP_REQUEST (0xEDFA).
             * Retained RAM only: no UICR, flash, PMIC or protection change. */
            *(volatile uint32_t *)0x2003f380u=0xedfau;
            __DSB();sys_reboot(SYS_REBOOT_COLD);
        }else if(k_uptime_get()>dfu_deadline)dfu_status=-ETIMEDOUT;
    }
}
int main(void){
    /* Binding uses Particle's USB ID -> exact FICR words; no alias selects HW. */
    if(NRF_FICR->DEVICEID[0]!=MHAT_FICR0||NRF_FICR->DEVICEID[1]!=MHAT_FICR1)return -EACCES;
    /* Pi power pins D8/P1.03, A3/P0.29 and PMIC are never configured here. */
    nrf_gpio_cfg_output(34);nrf_gpio_pin_clear(34); /* PPS low before bus connect */
    nrf_gpio_cfg_output(3);nrf_gpio_pin_set(3); /* A0/SEL: connect UART/PPS switch */
    bus_error=device_is_ready(bus)?uart_irq_callback_user_data_set(bus,bus_irq,NULL):-ENODEV;
    if(!bus_error){uart_irq_rx_disable(bus);uart_irq_tx_disable(bus);}
    if(!NRF_WDT->RUNSTATUS){NRF_WDT->CONFIG=1;NRF_WDT->CRV=5*32768;NRF_WDT->RREN=1;NRF_WDT->TASKS_START=1;}
    struct r2_identity id={.battery=-1};
    uint8_t hw[8];if(hwinfo_get_device_id(hw,sizeof hw)!=sizeof hw)return -ENODEV;
    for(unsigned i=0;i<8;i++)snprintf(id.silicon+2*i,3,"%02x",hw[i]);
    strcpy(id.unit,"MHAT1");sys_rand_get(&id.boot,sizeof id.boot);if(!id.boot)id.boot=1;
    bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM};sys_rand_get(address.a.val,6);address.a.val[5]|=0xc0;
    int rc=bt_id_create(&address,NULL);
    if(rc==0)rc=mhat_clock_start((uint32_t)id.boot?(uint32_t)id.boot:1);
    if(!rc)rc=mhat_pps_init();
    if(!rc)rc=bt_enable(NULL);
    (void)usb_enable(NULL);
    if(!rc)rc=mhat_mesh_start(&id);
    static char output[8192];uint64_t next=0;static struct r2_service_status status;
    static uint64_t source_root,source_parent,source_boot,source_seen;
    static uint32_t source_epoch;
    for(;;){
        watchdog();r2_capture_poll(k_ticks_to_us_floor64(k_uptime_ticks()));
        if(!rc){rc=mhat_mesh_poll();if(!dfu_requested)mhat_pps_poll();}
        if(device_is_ready(console))commands(id.boot);
        uint64_t now=k_uptime_get();
        if(now>=next){
            memset(&status,0,sizeof status);r2_dynamic_service_snapshot(&status);uint32_t pps[4];mhat_pps_stats(pps);
            r2_timing_sample(&status,now);
            int n=snprintf(output,sizeof output,"{\"target\":\"mhat-b504e\",\"version\":\"0.5.5\",\"source\":\"%s\",\"device_id\":\"%s\",\"particle_id\":\"%s\",\"boot\":\"%016llx\",\"uptime_ms\":%llu,\"error\":%d,\"receiver_error\":%d,\"capture_error\":%d,\"uart_error\":%d,\"dfu_status\":%d,\"cycles\":%u,\"active\":%s,\"rx\":%u,\"authenticated\":%u,\"rejected\":%u,\"following\":%s,\"clock_valid\":%s,\"pps_emitted\":%u,\"pps_rejected\":%u,\"pps_armed\":%u,\"offer_valid\":%u,\"pps_pin\":\"P1.02\",\"pi_gpio\":16,\"utc_accuracy_qualified\":false",MHAT_SOURCE_SHA,id.silicon,MHAT_PARTICLE_ID,(unsigned long long)id.boot,(unsigned long long)now,rc,status.error,r2_capture_error(),bus_error,dfu_status,status.cycles,status.active?"true":"false",status.rx,status.authenticated,status.rejected,status.follower?"true":"false",status.follower_clock_valid?"true":"false",pps[0],pps[1],pps[2],pps[3]);
            uint32_t transport[13],acquisition[20],admission[7],connection[20],mesh[6],plan[5];mhat_mesh_stats(mesh);mhat_pps_debug(plan);
            r2_dynamic_service_transport_snapshot(transport);r2_dynamic_service_acquisition_snapshot(acquisition);
            r2_dynamic_service_gm_admission_snapshot(admission);r2_dynamic_service_connection_snapshot(connection);
            struct r2_radio_capture_stats radio;r2_radio_capture_stats(&radio);
            uint32_t captures[]={radio.completed,radio.aborted,radio.invalid,radio.overflow,
                radio.rx.armed,radio.rx.completed,radio.rx.aborted,radio.rx.no_done,radio.rx.no_address,radio.rx.bad_crc};
            size_t at=n>0?(size_t)n:sizeof output;
            if(at<sizeof output&&array(output,sizeof output,&at,"transport",transport,13)&&
                array(output,sizeof output,&at,"acquisition",acquisition,20)&&
                array(output,sizeof output,&at,"admission",admission,7)&&
                array(output,sizeof output,&at,"connection",connection,20)&&
                array(output,sizeof output,&at,"mesh",mesh,6)&&
                array(output,sizeof output,&at,"radio",captures,10)&&
                array(output,sizeof output,&at,"pps_plan",plan,5)){
                n=snprintf(output+at,sizeof output-at,
                    ",\"path\":[\"%016llx\",\"%016llx\",\"%016llx\",\"%016llx\"],"
                    "\"fit\":{\"locked\":%s,\"samples\":%u,\"rms_valid\":%s,\"rms_ns\":%u,"
                    "\"drift_valid\":%s,\"drift_ppb\":%d,\"age_valid\":%s,\"age_ms\":%u}",
                    (unsigned long long)status.identities[0],(unsigned long long)status.identities[1],
                    (unsigned long long)status.identities[2],(unsigned long long)status.identities[3],
                    status.precision.locked?"true":"false",status.precision.samples,
                    status.precision.rms_valid?"true":"false",status.precision.rms_ns,
                    status.precision.drift_valid?"true":"false",status.precision.drift_ppb,
                    status.precision.correction_age_valid?"true":"false",status.precision.correction_age_ms);
                if(n>0&&(size_t)n<sizeof output-at){
                    at+=(size_t)n;
                    if(status.follower_clock_valid&&status.relay.root_identity){
                        source_root=status.relay.root_identity;source_parent=status.relay.parent_identity;
                        source_boot=status.root_boot;source_epoch=status.source_epoch;source_seen=now;
                    }
                    n=snprintf(output+at,sizeof output-at,",\"clock_source\":{\"valid\":%s,"
                        "\"root\":\"%016llx\",\"parent\":\"%016llx\",\"root_boot\":\"%016llx\","
                        "\"epoch\":%u,\"last_valid_ms\":%llu}",status.follower_clock_valid?"true":"false",
                        (unsigned long long)source_root,(unsigned long long)source_parent,
                        (unsigned long long)source_boot,source_epoch,(unsigned long long)source_seen);
                    if(n<0||(size_t)n>=sizeof output-at)goto sample_done;
                    at+=(size_t)n;n=mhat_mesh_json(output+at,sizeof output-at);
                    if(n<0)goto sample_done;
                    at+=(size_t)n;static char event[768];static uint32_t cursor;
                    int count=r2_timing_json(event,sizeof event,id.boot,cursor);
                    if(count>0&&at+(size_t)count+24<sizeof output){
                        n=snprintf(output+at,sizeof output-at,",\"timing_event\":%s",event);at+=(size_t)n;
                        char *sequence=strstr(event,"\"sequence\":");
                        if(sequence){uint32_t seq=(uint32_t)strtoul(sequence+11,NULL,10);if(seq>cursor)cursor=seq;}
                    }
                    if(at+4<sizeof output){memcpy(output+at,"}\r\n",4);line(output);}
                }
            }
sample_done:next=now+1000;
        }
        k_sleep(K_MSEC(5));
    }
}
