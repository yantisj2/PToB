// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ambient_light.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace corese;
struct Bus {
 uint8_t r[256]{};bool absent=false,fail=false,write_fail=false;unsigned sensor_writes=0,outputs=0;
 Bus(){r[0x86]=0x92;r[0x87]=5;r[0x8c]=4;}
 bool read(uint8_t a,uint8_t reg,uint8_t* out,unsigned n){assert(a==0x23);if(absent||fail)return false;std::memcpy(out,r+reg,n);return true;}
 bool write(uint8_t a,uint8_t reg,uint8_t value){assert(a==0x23);++sensor_writes;r[reg]=value;return !fail;}
 bool brightness(uint8_t value,uint8_t& result){assert(value>=32);++outputs;result=AmbientLight::code(value);return !write_fail;}
 void counts(uint16_t ch0,uint16_t ch1){r[0x88]=uint8_t(ch1);r[0x89]=uint8_t(ch1>>8);r[0x8a]=uint8_t(ch0);r[0x8b]=uint8_t(ch0>>8);}
};
int main(){
 int request=-1;assert(brightness_command("ui brightness auto",request)&&request==0);
 assert(brightness_command("ui brightness 32",request)&&request==32);
 assert(brightness_command("ui brightness 255",request)&&request==255);
 for(const char* text:{"ui brightness 0","ui brightness 31","ui brightness 256","ui brightness -1","ui brightness 99x","ui brightness 99 ","ui brightness 99999"})assert(!brightness_command(text,request));
 Bus bus;AmbientLight light;uint64_t at=1000000;light.begin(at,bus);
 assert(light.state().present&&light.state().configured&&light.state().verified);
 assert(bus.r[0x80]==1&&bus.r[0x85]==2&&!(bus.r[0x81]&3));
 auto run=[&](unsigned count){while(count--){at+=250000;light.poll(at,true,bus);assert(light.state().current>=150&&light.state().target>=150);}};
 bus.counts(100,100);run(20);assert(light.state().current==192&&light.state().raw==100);
 // A single dark sample / brief hand shadow must not dim the panel.
 bus.counts(0,0);run(1);bus.counts(100,100);run(8);assert(light.state().current==192);
 bus.counts(0,0);run(40);assert(light.state().current==150&&light.state().pmic_code==24&&light.state().fresh);
 bus.counts(65535,0);run(16);assert(light.state().current==255&&light.state().saturated);
 // Noise around an edge stays inside the20% hysteresis band.
 bus.counts(100,100);run(40);assert(light.state().current==192);
 for(unsigned i=0;i<80;++i){bus.counts(i%2?49:51,i%2?49:51);run(1);}assert(light.state().current==192);
 const auto last=light.state().sample_us;bus.r[0x8c]=0;run(16);
 assert(light.state().sample_us==last&&!light.state().fresh&&light.state().target==150);
 bus.r[0x8c]=0x84;run(4);assert(light.state().invalid==4&&light.state().sample_us==last);
 bus.r[0x8c]=0x14;run(4);assert(light.state().invalid==8); // Gain mismatch.
 bus.r[0x8c]=4;bus.fail=true;run(16);assert(light.state().target==150&&!light.state().fresh&&light.state().errors);
 bus.fail=false;bus.counts(10,10);run(28);assert(light.state().fresh&&light.state().current==160);
 assert(!light.manual(0)&&!light.manual(31)&&!light.manual(256));assert(light.manual(224));run(10);assert(!light.state().automatic&&light.state().current==224);
 light.automatic();run(32);assert(light.state().automatic&&light.state().current==160);
 const auto writes=bus.outputs;for(unsigned i=0;i<30;++i){at+=250000;light.poll(at,false,bus);}assert(bus.outputs==writes);
 light.poll(1,true,bus);assert(!light.state().fresh&&light.state().target==150);
 Bus missing;missing.absent=true;AmbientLight no_sensor;no_sensor.begin(1000000,missing);no_sensor.poll(5000000,true,missing);
 assert(!no_sensor.state().present&&no_sensor.state().current==150&&missing.sensor_writes==0);
 Bus wrong;wrong.r[0x87]=7;AmbientLight foreign;foreign.begin(1000000,wrong);assert(!foreign.state().present&&wrong.sensor_writes==0);
 Bus fault;fault.write_fail=true;AmbientLight retry;retry.begin(1000000,fault);retry.poll(2000000,true,fault);assert(!retry.state().verified);
 fault.write_fail=false;retry.poll(3000000,true,fault);assert(retry.state().verified&&retry.state().current==150);
 std::puts("PASS actual ALS driver/controller: exact ID/config, raw units, saturation/darkness, median/EMA, hysteresis, asymmetric delay, stale/invalid/error fallback, manual/AUTO, verified output, rollback and sleep priority");
}
