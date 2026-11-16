// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual selected Bosch parser/HIF; synthetic FIFO bytes, no hardware.
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#ifndef VENDOR_BHY2_SOURCE
#define VENDOR_BHY2_SOURCE "../vendor/bhi260/bhy2.c"
#endif
#include VENDOR_BHY2_SOURCE
static unsigned callbacks;
static unsigned metadata;
static void meta_sample(const struct bhy2_fifo_parse_data_info *info, void *ref)
{
    (void)ref;
    assert((info->sensor_id == BHY2_SYS_ID_META_EVENT || info->sensor_id == BHY2_SYS_ID_META_EVENT_WU) && info->data_size == 4);
    ++metadata;
}
static void sample(const struct bhy2_fifo_parse_data_info *info, void *ref)
{
    (void)ref;
    assert(info->sensor_id == BHY2_SENSOR_ID_ACC && info->data_size == 7);
    ++callbacks;
}
struct Wire { uint8_t status,reg; const uint8_t *bytes; unsigned length,offset,calls; bool size_read; };
static int8_t read_wire(uint8_t reg,uint8_t *bytes,uint32_t length,void *ctx)
{
    struct Wire *w=ctx;++w->calls;
    if(reg==BHY2_REG_INT_STATUS){assert(length==1);*bytes=w->status;return 0;}
    assert(reg==w->reg);
    if(!w->size_read){assert(length==2);bytes[0]=(uint8_t)w->length;bytes[1]=(uint8_t)(w->length>>8);w->size_read=true;return 0;}
    assert(length<=w->length-w->offset);memcpy(bytes,w->bytes+w->offset,length);w->offset+=length;return 0;
}
static int8_t write_wire(uint8_t reg,const uint8_t *bytes,uint32_t length,void *ctx)
{(void)reg;(void)bytes;(void)length;(void)ctx;assert(0);return -1;}
static void delay_wire(uint32_t us,void *ctx){(void)us;(void)ctx;}
static struct bhy2_dev device(void)
{
    struct bhy2_dev d={0};d.event_size[BHY2_SENSOR_ID_ACC]=7;
    assert(bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_ACC,sample,NULL,&d)==BHY2_OK);return d;
}
static void unknown_zero(void)
{
    struct bhy2_dev d=device();uint8_t data[]={127};
    struct bhy2_fifo_buffer f={0};f.buffer=data;f.buffer_size=f.read_length=sizeof data;
    assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_E_INVALID_PARAM);
    assert(!callbacks&&!f.read_pos&&f.read_length==1); // No guessed boundary or event.
}
static void wide_compaction(void)
{
    uint8_t data[300];for(unsigned i=0;i<sizeof data;++i)data[i]=(uint8_t)i;
    struct bhy2_fifo_buffer f={0};f.buffer=data;f.buffer_size=f.read_length=sizeof data;f.read_pos=1;
    assert(parse_fifo_support(&f)==BHY2_OK&&f.read_length==299);
    for(unsigned i=0;i<299;++i)assert(data[i]==(uint8_t)(i+1));
}
int main(int argc,char **argv)
{
    if(argc>1){if(!strcmp(argv[1],"zero"))unknown_zero();else wide_compaction();return 0;}
    unknown_zero();wide_compaction();
    struct bhy2_dev d=device();uint8_t data[]={0,BHY2_SYS_ID_TS_SMALL_DELTA,5,BHY2_SENSOR_ID_ACC,1,2,3,4,5,6,255};
    struct bhy2_fifo_buffer f={0};f.buffer=data;f.buffer_size=f.read_length=sizeof data;
    assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_OK&&!f.read_length&&callbacks==1);
    assert(d.last_time_stamp[BHY2_FIFO_TYPE_NON_WAKEUP]==5);
    // Incomplete known records retain bytes for the same FIFO's next chunk.
    callbacks=0;uint8_t partial[]={BHY2_SENSOR_ID_ACC,1,2,3,4,5,6};
    f=(struct bhy2_fifo_buffer){0};f.buffer=partial;f.buffer_size=sizeof partial;f.read_length=4;
    assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_OK&&f.read_length==4&&!callbacks);
    f.read_length=7;f.read_pos=0;assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_OK&&!f.read_length&&callbacks==1);
    uint8_t ts[]={BHY2_SYS_ID_TS_SMALL_DELTA,2};f=(struct bhy2_fifo_buffer){0};f.buffer=ts;f.buffer_size=2;f.read_length=1;
    assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_OK&&f.read_length==1);
    f.read_length=2;f.read_pos=0;assert(parse_fifo(BHY2_FIFO_TYPE_NON_WAKEUP,&f,&d)==BHY2_OK&&!f.read_length);
    for(unsigned wake=0;wake<2;++wake){
        const uint8_t bad[]={127};struct Wire wire={0};wire.status=wake?BHY2_IST_FIFO_W_DRDY:BHY2_IST_FIFO_NW_DRDY;wire.reg=wake?BHY2_REG_CHAN_FIFO_W:BHY2_REG_CHAN_FIFO_NW;wire.bytes=bad;wire.length=1;
        assert(bhy2_init(BHY2_I2C_INTERFACE,read_wire,write_wire,delay_wire,128,&wire,&d)==BHY2_OK);
        uint8_t buffer[512];assert(bhy2_get_and_process_fifo(buffer,sizeof buffer,&d)==BHY2_E_INVALID_PARAM&&wire.calls==3);
        // Firmware boot delivers system META before querying virtual sensors.
        // Its fixed length is lazily populated by the original callback lookup.
        uint8_t boot_meta[]={wake?BHY2_SYS_ID_META_EVENT_WU:BHY2_SYS_ID_META_EVENT,BHY2_META_EVENT_INITIALIZED,0,0};
        wire.size_read=false;wire.offset=wire.calls=0;wire.bytes=boot_meta;wire.length=sizeof boot_meta;
        assert(!d.event_size[boot_meta[0]]);
        assert(bhy2_register_fifo_parse_callback(boot_meta[0],meta_sample,NULL,&d)==BHY2_OK);
        metadata=0;assert(bhy2_get_and_process_fifo(buffer,sizeof buffer,&d)==BHY2_OK&&metadata==1&&d.event_size[boot_meta[0]]==4);
        // Ordinary known records still pass through the public FIFO/HIF path.
        wire.size_read=false;wire.offset=wire.calls=0;wire.bytes=partial;wire.length=7;d.event_size[BHY2_SENSOR_ID_ACC]=7;
        assert(bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_ACC,sample,NULL,&d)==BHY2_OK);
        callbacks=0;assert(bhy2_get_and_process_fifo(buffer,sizeof buffer,&d)==BHY2_OK&&callbacks==1&&wire.calls==3);
    }
    puts("PASS actual Bosch FIFO: zero-size refusal, wake/nonwake error propagation, normal timestamp/padding/sample records, partial-record continuation, wide compaction; synthetic bytes only");
}
