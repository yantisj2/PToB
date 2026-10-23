// Synthetic RadioLib call boundary. Native builds compile pinned real RadioLib.
#pragma once
#include <cstdint>
#include <cstddef>
using RadioLibTime_t=uint32_t;
constexpr uint32_t RADIOLIB_NC=0xffffffff;constexpr int RADIOLIB_ERR_NONE=0;
constexpr uint32_t RADIOLIB_LR2021_IRQ_ALL=0xffffffff,RADIOLIB_LR2021_IRQ_CRC_ERROR=4,RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR=8,RADIOLIB_LR2021_IRQ_RX_DONE=2;
constexpr uint8_t RADIOLIB_LR2021_DIO_FUNCTION_RF_SWITCH=0x10,RADIOLIB_LR2021_DIO_SLEEP_PULL_UP=1,RADIOLIB_LR2021_DIO_SLEEP_PULL_AUTO=0;
constexpr uint16_t RADIOLIB_LR2021_CMD_SET_DIO_FUNCTION=0x112,RADIOLIB_LR2021_CMD_SET_DIO_RF_SWITCH_CONFIG=0x113;
class RadioLibHal {public:
 RadioLibHal(int,int,int,int,int,int){};virtual ~RadioLibHal()=default;
 virtual void init()=0;virtual void term()=0;virtual void pinMode(uint32_t,uint32_t)=0;virtual void digitalWrite(uint32_t,uint32_t)=0;virtual uint32_t digitalRead(uint32_t)=0;
 virtual void attachInterrupt(uint32_t,void(*)(),uint32_t)=0;virtual void detachInterrupt(uint32_t)=0;
 virtual void delay(RadioLibTime_t)=0;virtual void delayMicroseconds(RadioLibTime_t)=0;virtual RadioLibTime_t millis()=0;virtual RadioLibTime_t micros()=0;virtual long pulseIn(uint32_t,uint32_t,RadioLibTime_t)=0;
 virtual void spiBegin()=0;virtual void spiEnd()=0;virtual void spiBeginTransaction()=0;virtual void spiEndTransaction()=0;virtual void spiTransfer(uint8_t*,size_t,uint8_t*)=0;virtual void yield()=0;
};
inline uint8_t test_dio_modes[12]{},test_dio_functions[12]{};inline unsigned test_irq_dio=0,test_irq_gpio=0;
class Module {public:RadioLibHal* hal;struct{unsigned timeout=0;}spiConfig;
 Module(RadioLibHal* h,int,int irq,int,int):hal(h){test_irq_gpio=unsigned(irq);}
 int SPIwriteStream(uint16_t op,const uint8_t* args,size_t n){if((op==0x112||op==0x113)&&n==2){(op==0x112?test_dio_functions:test_dio_modes)[args[0]]=args[1];}uint8_t t[]={uint8_t(op>>8),uint8_t(op)},r[2];hal->spiTransfer(t,2,r);return 0;}
};
inline uint32_t test_irq=0;inline size_t test_size=80;inline uint8_t test_packet[128]{};
class LR2021 {Module* mod;public:unsigned irqDioNum=0;enum {MODE_RX=2};
 LR2021(Module* m):mod(m){};
 int begin(float,float,int,int,int,int,int,float){test_irq_dio=irqDioNum;mod->hal->init();mod->hal->digitalWrite(12,1);return mod->SPIwriteStream(0x100,nullptr,0);}
 int setCRC(int){return 0;}int explicitHeader(){return 0;}int standby(){return 0;}
 int clearIrqFlags(uint32_t){test_irq=0;return 0;}int startReceive(){return mod->SPIwriteStream(0x20c,nullptr,0);}
 uint32_t getIrqFlags(){return test_irq;}size_t getPacketLength(){return test_size;}int readData(uint8_t* p,size_t n){for(size_t i=0;i<n;i++)p[i]=test_packet[i];return 0;}
 float getRSSI(){return -70;}float getSNR(){return 6;}
};
