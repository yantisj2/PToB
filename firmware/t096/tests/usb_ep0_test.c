/* Tests are compiled against exact extracted stock/fixed driver bodies. */
static void ev_epdata_handler(uint32_t bits){if(bits&BIT(ep2bit(0x80))){nrf_usbd_common_evt_t e={.data.eptransfer={0x80,NRF_USBD_COMMON_EP_OK}};usbd_event_transfer_ctrl(&e);}}
static void next_setup(uint8_t type,uint8_t req,uint16_t length){regs.BMREQUESTTYPE=type;regs.BREQUEST=req;regs.WLENGTHL=length;regs.WLENGTHH=length>>8;ev_setup_handler();}
static void stack_callback(uint8_t ep,int status){
 assert(!irq_depth);++callbacks[status];
 if(status==USB_DC_EP_SETUP){
  struct usb_setup_packet s;assert(!usb_dc_ep_read_wait(ep,(uint8_t *)&s,sizeof s,NULL));assert(!usb_dc_ep_read_continue(ep));
  if(inject_setup){inject_setup=false;next_setup(0xc1,0x53,256);}
  if(write_callback)callback_write_result=usb_dc_ep_write(0x80,epbuf[0],64,&callback_write_bytes);
 }
}
static void reset(void){
 memset(&usbd_ctx,0,sizeof usbd_ctx);memset(endpoints,0,sizeof endpoints);memset(events,0,sizeof events);memset(callbacks,0,sizeof callbacks);
 allocated=head=tail=irq_depth=setup_clear=data_clear=submissions=out_submissions=0;inject_setup=inject_mutex=false;write_callback=true;
 for(unsigned i=0;i<3;i++){endpoints[i].cfg.cb=stack_callback;endpoints[i].cfg.max_sz=64;endpoints[i].cfg.en=true;endpoints[i].cfg.addr=i==0?0x80:i==1?0:0x81;endpoints[i].cfg.type=i<2?USB_DC_EP_CONTROL:USB_DC_EP_BULK;endpoints[i].buf.data=endpoints[i].buf.curr=epbuf[i];}
}
static void process_one(void){assert(head<tail);usbd_work_process_ep_events(&events[queued[head++]].evt.ep_evt);}
static void drain(void){while(head<tail)process_one();}
static void transfer(unsigned status){nrf_usbd_common_evt_t e={.data.eptransfer={0x80,status}};usbd_event_transfer_ctrl(&e);}
int main(void){
 reset();next_setup(0xc1,0x53,256);drain();assert(callback_write_result==0&&endpoints[0].write_in_progress);transfer(NRF_USBD_COMMON_EP_OK);drain();assert(!endpoints[0].write_in_progress&&callbacks[USB_DC_EP_DATA_IN]==1);
 /* Host replaces an unacknowledged IN. Pinned lower SETUP clears pending DMA
  * without EP_ABORTED. Stock driver reproduces permanent software busy. */
 reset();next_setup(0xc1,0x53,256);drain();endpoints[0].trans_zlp=true;next_setup(0xc1,0x52,256);
#ifdef FIXED
 assert(!endpoints[0].write_in_progress&&!endpoints[0].trans_zlp);
#endif
 drain();
#ifdef FIXED
 assert(callback_write_result==0&&usbd_ctx.ep0_diagnostics[1]==1);
#else
 assert(callback_write_result==-EAGAIN&&endpoints[0].trans_zlp);
#endif
 /* Direct ABORT is synchronous; it must release busy/ZLP, without callback. */
 reset();next_setup(0xc1,0x53,256);drain();endpoints[0].trans_zlp=true;transfer(NRF_USBD_COMMON_EP_ABORTED);
#ifdef FIXED
 assert(!endpoints[0].write_in_progress&&!endpoints[0].trans_zlp&&callbacks[USB_DC_EP_DATA_IN]==0);
#else
 assert(endpoints[0].write_in_progress&&endpoints[0].trans_zlp);
#endif
 /* A completion queued before SETUP must never finish the replacement. */
 reset();next_setup(0xc1,0x53,256);drain();transfer(NRF_USBD_COMMON_EP_OK);next_setup(0xc1,0x52,256);drain();
#ifdef FIXED
 assert(callbacks[USB_DC_EP_DATA_IN]==0&&endpoints[0].write_in_progress&&usbd_ctx.ep0_diagnostics[3]==1);
#else
 assert(callbacks[USB_DC_EP_DATA_IN]==1);
#endif
 /* New SETUP interrupts previous application callback. Old IN submission must
  * be rejected, then newest SETUP must submit successfully. */
 reset();inject_setup=true;next_setup(0xc1,0x52,256);process_one();
#ifdef FIXED
 assert(callback_write_result==-ECANCELED&&callback_write_bytes==0&&submissions==0);
#else
 assert(callback_write_result==0&&submissions==1);
#endif
 drain();
#ifdef FIXED
 assert(callback_write_result==0&&submissions==1&&endpoints[0].write_in_progress);
#endif
 /* Completion filter checked, then SETUP IRQ while waiting for driver mutex.
  * Hardware status clear still must not act on the new request. */
 reset();next_setup(0xc1,0x55,64);drain();transfer(NRF_USBD_COMMON_EP_OK);inject_mutex=true;process_one();
#ifdef FIXED
 assert(setup_clear==0&&callbacks[USB_DC_EP_DATA_IN]==0);
#else
 assert(setup_clear==1&&callbacks[USB_DC_EP_DATA_IN]==1);
#endif
 drain();
 /* OUT setup unarms old receive flags, stack read rearms via unchanged API. */
 reset();write_callback=false;endpoints[1].read_pending=true;next_setup(0x41,0x54,0);drain();
#ifdef FIXED
 assert(endpoints[1].read_complete&&!endpoints[1].read_pending&&out_submissions==0);
#endif
 next_setup(0x41,0x54,1);drain();assert(data_clear==1&&endpoints[1].read_complete);
 /* Exact common IRQ dispatcher defers SETUP while EP0 DMA is active. Once
  * DMA finishes, old DATA completion is enqueued before replacement SETUP. */
 reset();next_setup(0xc1,0x55,64);drain();
 dma_available=0;dma_ep=0x80;dma_endevent=0;regs.EVENTS_EP0SETUP=1;regs.BREQUEST=0x53;
 uint32_t generation=usbd_ctx.ep0_generation;nrf_usbd_common_irq_handler();assert(regs.EVENTS_EP0SETUP==1&&usbd_ctx.ep0_generation==generation);
 dma_endevent=1;regs.EVENTS_EP0DATADONE=1;nrf_usbd_common_irq_handler();assert(regs.EVENTS_EP0SETUP==0);
#ifdef FIXED
 assert(events[queued[head]].evt.ep_evt.generation==generation&&events[queued[head+1]].evt.ep_evt.generation==generation+1);
#endif
 drain();
#ifdef FIXED
 assert(callbacks[USB_DC_EP_DATA_IN]==0&&endpoints[0].write_in_progress);
#endif
 /* Bulk endpoint admission is unchanged and never enters the new IRQ guard. */
 reset();uint32_t n=0;assert(!usb_dc_ep_write(0x81,epbuf[0],64,&n)&&n==64);assert(usb_dc_ep_write(0x81,epbuf[0],64,&n)==-EAGAIN);
#ifdef FIXED
 for(unsigned i=0;i<100;i++){reset();next_setup(0xc1,0x53,256);drain();next_setup(0x41,0x54,0);drain();next_setup(0xc1,0x55,64);drain();assert(callback_write_result==0);}
 puts("PASS exact fixed EP0 normal, replacement, ABORTED, stale completion/callback, status race, OUT rearm, bulk and 100 reopen sequences and exact IRQ active-DMA ordering");
#else
 puts("REPRODUCED exact stock EP0 replacement/ABORT busy latch and stale callback/status races");
#endif
 return 0;
}
