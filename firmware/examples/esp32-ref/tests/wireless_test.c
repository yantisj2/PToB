// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_wire.h"
#include "ref_session.h"
#include "ref_join.h"
#include "ref_button.h"
#include "ref_display.h"
#include "ref_shutdown.h"
#include "radio_math.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while (0)

// Deliberately NONCRYPTOGRAPHIC test fixture. It tests codec boundaries and
// tamper/refusal paths. Firmware only links the SDK HMAC-SHA256 backend.
static bool fixture_sign(const uint8_t *data, size_t n, uint8_t tag[16])
{
    memset(tag, 0x5a, 16);
    for (size_t i = 0; i < n; ++i) tag[i % 16] = (uint8_t)((tag[i % 16] * 33u) ^ data[i]);
    return true;
}

int main(void)
{
    uint8_t bytes[REF_WIRE_BYTES]; RefFrame decoded;
    RefFrame frame = {.kind=REF_BEACON,.sender=1,.session=2,.publication=3,.reply_to=4,.nonce=5,.count=1};
    frame.corrections[0] = (RefCorrection){2,65535,36,1,INT64_C(1000000000)};
    CHECK(ref_wire_encode(&frame,fixture_sign,bytes));
    CHECK(ref_wire_decode(bytes,sizeof(bytes),fixture_sign,&decoded));
    CHECK(decoded.corrections[0].event==65535 && decoded.corrections[0].time_ns==1000000000);
    for (unsigned i=0;i<REF_WIRE_BYTES;++i) {
        bytes[i]^=1; CHECK(!ref_wire_decode(bytes,sizeof(bytes),fixture_sign,&decoded)); bytes[i]^=1;
    }
    CHECK(!ref_wire_decode(bytes,sizeof(bytes)-1,fixture_sign,&decoded));
    bytes[100]=1; CHECK(fixture_sign(bytes,REF_WIRE_AUTH_BYTES,bytes+REF_WIRE_AUTH_BYTES));
    CHECK(!ref_wire_decode(bytes,sizeof(bytes),fixture_sign,&decoded));

    RefSession s; ref_session_select(&s,4,1,2,5);
    CHECK(ref_session_accept(&s,&frame,100));
    const uint64_t original_deadline=s.until_tick;
    CHECK(original_deadline == 100 + REF_MS_TICKS(6000));
    frame.publication++;
    CHECK(ref_session_accept(&s,&frame,200) && s.until_tick==original_deadline);
    CHECK(!ref_session_accept(&s,&frame,201)); // Replay cannot refresh progress.
    frame.publication++; frame.session=9;
    CHECK(!ref_session_accept(&s,&frame,202));
    frame.session=2; CHECK(!ref_session_accept(&s,&frame,original_deadline));
    ref_session_challenge(&s,7); frame.publication++; frame.nonce=5;
    CHECK(!ref_session_accept(&s,&frame,original_deadline+1));
    frame.publication++; frame.nonce=7;
    CHECK(ref_session_accept(&s,&frame,original_deadline+2));

    RefJoin join={0}; RefOriginal original;
    RefCorrection c={7,0,3,1,100}; RefPrediction prediction={99,true};
    CHECK(ref_join_original(&join,7,0,3,100,prediction));
    CHECK(!ref_join_original(&join,7,0,3,101,prediction));
    CHECK(ref_join_correction(&join,&c,200,&original) && original.prediction.ns==99);
    CHECK(!ref_join_correction(&join,&c,201,&original));
    CHECK(ref_join_original(&join,8,1,4,300,prediction)); c=(RefCorrection){8,1,4,1,100};
    CHECK(!ref_join_correction(&join,&c,301+REF_MS_TICKS(250),&original));

    RefButton b={0};
    for(uint64_t t=0;t<5000;t+=20) CHECK(ref_button_poll(&b,true,t)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,false,5000)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,false,5040)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,true,5100)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,true,5140)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,true,9139)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,true,9140)==REF_BUTTON_OFF);
    CHECK(ref_button_poll(&b,true,9200)==REF_BUTTON_NONE);
    CHECK(ref_button_poll(&b,false,9220)==REF_BUTTON_NONE);
    CHECK(!ref_button_released(&b,9259));
    CHECK(ref_button_poll(&b,false,9260)==REF_BUTTON_NONE && ref_button_released(&b,9260));

    RefShutdown shutdown={0};
    ref_shutdown_request(&shutdown,true,100);
    ref_shutdown_request(&shutdown,false,1000);
    CHECK(shutdown.deadline_ms==2100);
    CHECK(ref_shutdown_poll(&shutdown,2099,false,0,false,true)==REF_SHUTDOWN_DRAIN);
    CHECK(ref_shutdown_poll(&shutdown,2100,false,0,false,true)==REF_SHUTDOWN_FAULT);
    CHECK(ref_shutdown_poll(&shutdown,2101,true,2101,false,true)==REF_SHUTDOWN_FAULT);
    shutdown=(RefShutdown){0}; ref_shutdown_request(&shutdown,true,100);
    CHECK(ref_shutdown_poll(&shutdown,2000,true,1999,false,false)==REF_SHUTDOWN_RELEASE);
    CHECK(ref_shutdown_poll(&shutdown,2101,true,1999,false,true)==REF_SHUTDOWN_SLEEP);
    ref_shutdown_board_failed(&shutdown);
    CHECK(ref_shutdown_poll(&shutdown,2200,true,1999,false,true)==REF_SHUTDOWN_FAULT && shutdown.board_failure);
    shutdown=(RefShutdown){0}; ref_shutdown_request(&shutdown,false,100);
    CHECK(ref_shutdown_poll(&shutdown,2000,true,1999,false,true)==REF_SHUTDOWN_READY);
    shutdown=(RefShutdown){0}; ref_shutdown_request(&shutdown,true,100);
    CHECK(ref_shutdown_poll(&shutdown,2099,true,2101,false,true)==REF_SHUTDOWN_FAULT);

    const uint64_t scale = REF_CLOCK_HZ / 20000000;
    RefRadioBridge bridge={1000000*scale,1000020*scale,100,0}; uint64_t tick=0;
    CHECK(ref_radio_rx_tick(&bridge,100,624,0,&tick) && tick==1000010*scale);
    CHECK(ref_radio_rx_tick(&bridge,99,624,0,&tick) && tick==993760*scale);
    CHECK(ref_radio_tx_tick(&bridge,99,1000020*scale,&tick) && tick==994560*scale);
    CHECK(!ref_radio_tx_tick(&bridge,101,1000020*scale,&tick));
    bridge.slots=0; CHECK(ref_radio_rx_tick(&bridge,0x0fffffff,624,0,&tick) && tick==993760*scale);
    bridge.after=bridge.before+400*scale+1; CHECK(!ref_radio_rx_tick(&bridge,0,624,0,&tick));
    RefCaptureCounter capture = {0};
    CHECK(ref_capture32_advance(&capture, UINT32_MAX - 2, 10, &tick));
    CHECK(ref_capture32_advance(&capture, 1, 11, &tick) && tick == UINT64_C(0x100000001));
    CHECK(!ref_capture32_advance(&capture, 2, 26843557, &tick) && tick == UINT64_C(0x100000001));
    CHECK(!ref_capture32_advance(&capture, 3, 12, &tick));
    const uint16_t map[3]={0xffff,0xffff,0x1f}; uint8_t channel;
    CHECK(ref_radio_channel(0x8e89bed6,map,0,&channel) && channel<37);
    const uint16_t narrow[3]={3,0,0};
    for(unsigned event=0;event<65536;++event) CHECK(ref_radio_channel(0x12345678,narrow,(uint16_t)event,&channel) && channel<2);
    RefCounterHealth health={0};
    CHECK(ref_counter_health(&health,100,0));
    CHECK(ref_counter_health(&health,REF_MS_TICKS(20)+100,20000));
    CHECK(!ref_counter_health(&health,REF_MS_TICKS(20)+100,40000));
    CHECK(!ref_counter_health(&health,1200100,60000));
    health=(RefCounterHealth){0};
    CHECK(ref_counter_health(&health,100,1000));
    CHECK(!ref_counter_health(&health,99,2000));

    RefSoftPll pll; ref_pll_begin(&pll);
    CHECK(ref_pll_observe(&pll,100,200,1000,0,(RefPrediction){0,false}));
    CHECK(!ref_pll_observe(&pll,100,201,1000,0,prediction));
    CHECK(!ref_pll_observe(&pll,300,299,1000,0,prediction));
    CHECK(!ref_pll_observe(&pll,300,301+REF_MS_TICKS(250),1000,0,prediction));
    CHECK(!ref_pll_observe(&pll,300,400,1000000000,0,(RefPrediction){0,true}));
    CHECK(pll.state==REF_PLL_FAULT && pll.maximum_error_ns==1000000000);
    puts("PASS: codec/tamper fixtures, one-use admission, replay, event joins, button, BLE arithmetic and fault paths");
    return 0;
}
