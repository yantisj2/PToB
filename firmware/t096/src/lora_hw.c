/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * T096 SX1262 owner. Semtech DS.SX1261-2.W.APP rev2.2 sections13-15;
 * Heltec Mesh_Node_T096_V0.2 schematic / official FEM control table.
 * Public Zephyr SPI/GPIO and allocated nrfx TIMER4/GPIOTE/PPI only.
 */
#include "lora_hw.h"
#include "lora_watchdog.h"
#include "capture.h"
#include "radio_nrf5_resources.h"
#include <errno.h>
#include <nrfx_gpiote.h>
#include <nrfx_ppi.h>
#include <nrfx_timer.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#define R2_LOCK_UNIT 25u
#include "health_lock.h"

#define SPI_BYTES (R2_LORA_HW_BYTES + 3u)
#define CSD_PIN NRF_GPIO_PIN_MAP(0, 12)
#define TIMING_CUTOFF_US 360000u
#define DIAGNOSTIC_CUTOFF_US 450000u
#define BUSY_MS 30u
#define DMA_MS 10u
BUILD_ASSERT(EVENT_TIMER_ID != 3 && EVENT_TIMER_ID != 4);
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_NRF52840));
BUILD_ASSERT(!IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME));
BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(timer4), okay));
static const nrfx_timer_t cutoff = NRFX_TIMER_INSTANCE(4);
static const nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(0);
static const struct device *const spi = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct gpio_dt_spec reset =
    GPIO_DT_SPEC_GET(DT_NODELABEL(lora_reset), gpios);
static const struct gpio_dt_spec ctx =
    GPIO_DT_SPEC_GET(DT_NODELABEL(lora_ctx), gpios);
static const struct gpio_dt_spec power =
    GPIO_DT_SPEC_GET(DT_NODELABEL(lora_fem_power), gpios);
static const struct gpio_dt_spec busy =
    GPIO_DT_SPEC_GET(DT_NODELABEL(lora_busy), gpios);
static const struct gpio_dt_spec dio =
    GPIO_DT_SPEC_GET(DT_NODELABEL(lora_dio1), gpios);
static const struct spi_config spi_config = {
    .frequency = 8000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(lora_cs), gpios), .delay = 0}};
static K_SEM_DEFINE(completed, 0, 1);
static uint8_t dma_tx[SPI_BYTES], dma_rx[SPI_BYTES];
static struct spi_buf tx_buffer = {.buf = dma_tx}, rx_buffer = {.buf = dma_rx};
static const struct spi_buf_set tx_buffers = {.buffers = &tx_buffer,
                                              .count = 1};
static const struct spi_buf_set rx_buffers = {.buffers = &rx_buffer,
                                              .count = 1};
static struct gpio_callback dio_callback;
static struct r2_capture edge;
static uint64_t edge_us;
static uint64_t active_epoch;
static uint32_t generation, edge_generation;
static nrf_ppi_channel_t cutoff_channel;
static uint8_t task_channel;
static bool resources, capture_owned, receive_mode;
static atomic_t running, dma_inflight, result, fault, latched, phase,
    cutoff_expired, stop_serial, launch_aborted;
static struct r2_lora_hw_diag counters;
#define wd r2_diag_scratch.lora
/* phase:0 inhibited/reset,1 idle,2 single RX,3 retired TX command,
 * 4 software TX launch not yet retired. No SPI or waiting from IRQ. */
static uint64_t now_us(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }
static void count(uint32_t *v) {
  if (*v != UINT32_MAX)
    ++*v;
}
static void stage(unsigned value) {
  unsigned key=irq_lock();
  wd.stage=(value<<8)|dma_tx[0];wd.stage_at=k_uptime_get_32();
  irq_unlock(key);
}
static void note_fault(int rc,bool terminal) {
  unsigned key=irq_lock();
  /* Preserve the first terminal cause. A terminal cause supersedes an
   * uncommitted recoverable launch, counted as coalesced, never as saved. */
  if(wd.terminal||(wd.serial!=wd.saved&&!terminal)) {
    count(&wd.coalesced);irq_unlock(key);return;
  }
  if(wd.serial!=wd.saved)count(&wd.coalesced);
  if(wd.serial!=UINT32_MAX)++wd.serial;
  uint32_t at=k_uptime_get_32();
  uint32_t *p=wd.record;
  p[0]=R2_LORA_FAULT_MAGIC;p[1]=(uint32_t)rc;p[2]=at;
  p[3]=wd.stage;p[4]=wd.stage_at;
  p[5]=(uint32_t)atomic_get(&phase)|((uint32_t)!!atomic_get(&running)<<8)|
    ((uint32_t)!!atomic_get(&dma_inflight)<<9)|((uint32_t)atomic_get(&latched)<<10)|
    ((uint32_t)capture_owned<<12)|((uint32_t)receive_mode<<13)|
    ((uint32_t)atomic_get(&launch_aborted)<<14)|((uint32_t)!!atomic_get(&cutoff_expired)<<16);
  p[6]=generation;p[7]=(uint32_t)edge.epoch;p[8]=(uint32_t)edge.tick;p[9]=(uint32_t)(edge.tick>>32);
  int busy_value=gpio_pin_get_dt(&busy),dio_value=gpio_pin_get_dt(&dio);
  p[10]=wd.irq_status|((uint32_t)(busy_value>0)<<16)|((uint32_t)(dio_value>0)<<17)|
    ((uint32_t)(busy_value<0)<<18)|((uint32_t)(dio_value<0)<<19);
  p[11]=(uint32_t)atomic_get(&stop_serial);
  p[12]=counters.spi_ok;p[13]=counters.spi_error;p[14]=counters.cutoffs;
  p[15]=(counters.launch_aborts>65535?65535:counters.launch_aborts)|((uint32_t)counters.busy_aborts<<16);
  if(terminal){wd.terminal=1;wd.fault_at=at;}
  irq_unlock(key);
}
static void inhibit(void) {
  unsigned key = irq_lock();
  atomic_clear(&running);
  if (resources) {
    nrfx_gpiote_clr_task_trigger(&gpiote, CSD_PIN);
    (void)nrfx_ppi_channel_disable(cutoff_channel);
    nrfx_timer_disable(&cutoff);
  }
  int rc = gpio_pin_set_dt(&ctx, 0);
  int p = gpio_pin_set_dt(&power, 0), r = gpio_pin_set_dt(&reset, 1);
  if (rc || p || r)
    atomic_cas(&fault, 0, rc ? rc : p ? p : r);
  atomic_clear(&phase);
  irq_unlock(key);
}
static int fail(int rc) {
  note_fault(rc,true);
  atomic_cas(&fault, 0, rc);
  inhibit();
  return rc;
}
static void latch_edge(atomic_val_t state) {
  if (!r2_capture_lora(&edge)) {
    /* A delayed/invalid timestamp drops this observation, not BLE or the
     * radio owner. The worker still verifies cause and retires the FIFO.
     * Epoch zero can never become a valid projected timing event. */
    edge = (struct r2_capture){0};
    count(&counters.invalid_capture);
  }
  edge_us = now_us();
  edge_generation = generation;
  if (resources)
    nrfx_timer_disable(&cutoff);
  atomic_set(&latched, state);
}
static void cutoff_irq(nrf_timer_event_t event, void *unused) {
  if (event == NRF_TIMER_EVENT_COMPARE0 &&
      (atomic_get(&phase) == 3 || atomic_get(&phase) == 4) &&
      !atomic_get(&latched)) {
    /* Higher-priority BLE may delay the software DIO callback past this
     * compare. A pending physical DIO already captured CC4 and inhibited
     * the FEM. Latch it now; only the worker's IRQ-status read can classify
     * DONE versus chip timeout. Neither IRQ arrival becomes a timestamp. */
    if (gpio_pin_get_dt(&dio) > 0) {
      latch_edge(2); /* Ordinary DIO callback is still pending. */
      return;
    }
    /* The hardware compare -> CSD CLR route already inhibited the FEM. */
    if (atomic_get(&phase) == 4) {
      /* BLE can preempt this low-priority worker before/during SetTx. The
       * original hardware window still ends now: reset aborts any partial
       * RF and cancels further commands. No completed-launch claim exists.
       * This may have reached a listener, so the caller must retire the
       * proposed sequence and perform full bounded DMA/capture closure.
       * A real SPI error or a retired TX timeout remains permanently fatal. */
      atomic_set(&launch_aborted, 1);
      count(&counters.launch_aborts);
      note_fault(-EINTR,false);
      inhibit();
      return;
    }
    atomic_set(&cutoff_expired, 1);
    count(&counters.cutoffs);
    /* A preempted worker must not resume into SetTx after its FEM window.
     * Assert reset and cancel commands without SPI, including an already
     * submitted transfer. An earlier latched DONE retains its physical event.
     */
    (void)fail(-ETIMEDOUT);
  }
}
static void dio_irq(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins) {
  if (!atomic_get(&running))
    return;
  if (atomic_cas(&latched, 2, 1))
    return; /* The cutoff ISR already captured this exact pending edge. */
  /* Single RX/TX plus one IRQ cause: hardware has frozen FIFO and cleared
   * CSD via PPI before this callback. No callback-time timestamp substitute. */
  if (atomic_get(&latched)) {
    count(&counters.overflow);
    return;
  }
  latch_edge(1);
}
static void spi_done(const struct device *dev, int rc, void *unused) {
  atomic_set(&result, rc);
  atomic_clear(&dma_inflight);
  k_sem_give(&completed);
}
static int busy_deadline(void) {
  unsigned key = irq_lock();
  int rc = -ETIMEDOUT;
  if (!atomic_get(&running))
    rc = -ECANCELED;
  else {
    /* Re-read under the fence: a preempted old HIGH observation cannot turn
     * an already-ready chip into a timeout. No SPI or wait inside the fence. */
    int high = gpio_pin_get_dt(&busy);
    if (high <= 0)
      rc = high;
    else {
      counters.last_busy_op = dma_tx[0];
      if (dma_tx[0] == 0x83 && atomic_get(&phase) == 4 &&
          !atomic_get(&fault) && !atomic_get(&dma_inflight)) {
      /* Only an unretired SetTx launch can take this bounded reset path.
       * RF delivery is ambiguous; the caller consumes its sequence and
       * closes before retry. A stuck BUSY remains visible, never hidden as
       * a successful launch. Other operations and real DMA errors fault. */
      atomic_set(&launch_aborted, 2);
      count(&counters.launch_aborts);
      if (counters.busy_aborts != UINT16_MAX)
        ++counters.busy_aborts;
      note_fault(-EINTR,false);
      inhibit();
      rc = -ECANCELED;
      }
    }
  }
  irq_unlock(key);
  return rc;
}
static int wait_busy(void) {
  int64_t until = k_uptime_get() + BUSY_MS;
  for (;;) {
    if (!atomic_get(&running))
      return -ECANCELED;
    int value = gpio_pin_get_dt(&busy);
    if (value < 0)
      return value;
    if (!value)
      return 0;
    if (k_uptime_get() >= until)
      return busy_deadline();
    k_sleep(K_MSEC(1));
  }
}
static int transfer(size_t n) {
  if (!atomic_get(&running))
    return -ECANCELED;
  if (atomic_get(&dma_inflight) || atomic_get(&fault))
    return -EBUSY;
  stage(R2_LORA_STAGE_BUSY_BEFORE);
  int rc = wait_busy();
  if (rc)
    return rc;
  tx_buffer.len = rx_buffer.len = n;
  k_sem_reset(&completed);
  atomic_set(&dma_inflight, 1);
  stage(R2_LORA_STAGE_DMA);
  rc = spi_transceive_cb(spi, &spi_config, &tx_buffers, &rx_buffers, spi_done,
                         NULL);
  if (rc) {
    atomic_clear(&dma_inflight);
    count(&counters.spi_error);
    return fail(rc);
  }
  if (k_sem_take(&completed, K_MSEC(DMA_MS))) {
    /* Driver/DMA still owns both buffers AND descriptors. Permanent fault:
     * never mutate/retry/release/uninit these objects, even if callback late.
     */
    count(&counters.spi_error);
    return fail(-ETIMEDOUT);
  }
  rc = atomic_get(&result);
  if (rc) {
    count(&counters.spi_error);
    return fail(rc);
  }
  count(&counters.spi_ok);
  stage(R2_LORA_STAGE_BUSY_AFTER);
  return wait_busy();
}
static int command(uint8_t op, const uint8_t *args, size_t n) {
  if (!atomic_get(&running))
    return -ECANCELED;
  if (atomic_get(&fault) || atomic_get(&dma_inflight))
    return -EBUSY;
  if (n + 1 > SPI_BYTES)
    return -EINVAL;
  dma_tx[0] = op;
  if (n)
    memcpy(dma_tx + 1, args, n);
  return transfer(n + 1);
}
static bool status_ok(uint8_t s) {
  unsigned v = (s >> 1) & 7u;
  return s && s != 255 && v != 3 && v != 4 && v != 5;
}
static bool capture_live(void) {
  struct r2_capture current;
  return r2_capture_now(&current) && current.epoch == active_epoch;
}
static int reg_write(uint16_t reg, uint8_t value) {
  uint8_t a[] = {reg >> 8, reg, value};
  return command(0x0d, a, sizeof(a));
}
static int reg_read(uint16_t reg, uint8_t *value) {
  uint8_t a[] = {reg >> 8, reg, 0, 0};
  int rc = command(0x1d, a, sizeof(a));
  if (!rc && !status_ok(dma_rx[3]))
    rc = -EIO;
  if (!rc)
    *value = dma_rx[4];
  return rc;
}
static int packet_params(uint8_t length) {
  const uint8_t a[] = {0, 12, 0, length, 1, 0};
  return command(0x8c, a, sizeof(a));
}
static int clear_irq(void) {
  const uint8_t a[] = {0xff, 0xff};
  return command(0x02, a, sizeof(a));
}
static int mask_irq(bool tx) {
  /* DIO1 gets DONE, CRC/header-error or timeout only; no header/preamble edge
   * can overwrite CC4. A failed CRC is never exported as a timing event. */
  uint16_t flags = tx ? 0x201 : 0x262;
  uint8_t a[] = {flags >> 8, flags, flags >> 8, flags, 0, 0, 0, 0};
  return command(0x08, a, sizeof(a));
}
static int enable_fem(bool transmit, uint32_t budget, uint64_t latest_finish,
                      bool *deferred) {
  if (deferred)
    *deferred = false;
  unsigned key = irq_lock();
  int rc = -ECANCELED;
  if (atomic_get(&running) && !atomic_get(&fault)) {
    uint64_t now = now_us();
    if (transmit && latest_finish &&
        (now >= latest_finish ||
         (uint64_t)budget + R2_LORA_HW_LAUNCH_MARGIN_US > latest_finish - now)) {
      if (deferred)
        *deferred = true;
      irq_unlock(key);
      return -EAGAIN;
    }
    /* Official Heltec bypass RX: CSD=1,CTX=1,DIO2(CPS)=0. In TX DIO2=1.
     * Initial canary chip drive is0dBm; antenna power is not measured. */
    rc = gpio_pin_set_dt(&ctx, 1);
    if (!rc && transmit) {
      atomic_clear(&cutoff_expired);
      nrfx_timer_clear(&cutoff);
      nrfx_timer_extended_compare(&cutoff, NRF_TIMER_CC_CHANNEL0, budget,
                                  NRF_TIMER_SHORT_COMPARE0_STOP_MASK, true);
      rc = nrfx_ppi_channel_enable(cutoff_channel) == NRFX_SUCCESS ? 0 : -EIO;
      if (!rc)
        nrfx_timer_enable(&cutoff);
    }
    if (!rc)
      nrfx_gpiote_set_task_trigger(&gpiote, CSD_PIN);
  }
  irq_unlock(key);
  return rc;
}
static int init_resources(void) {
  if (resources)
    return 0;
  if (!device_is_ready(spi) || !nrfx_gpiote_init_check(&gpiote))
    return -ENODEV;
  if (gpio_pin_configure_dt(&busy, GPIO_INPUT) ||
      gpio_pin_configure_dt(&dio, GPIO_INPUT))
    return -EIO;
  if (nrfx_gpiote_channel_alloc(&gpiote, &task_channel) != NRFX_SUCCESS)
    return -ENOMEM;
  nrfx_gpiote_output_config_t output = {.drive = NRF_GPIO_PIN_S0S1,
                                        .input_connect =
                                            NRF_GPIO_PIN_INPUT_DISCONNECT,
                                        .pull = NRF_GPIO_PIN_NOPULL};
  nrfx_gpiote_task_config_t task = {.task_ch = task_channel,
                                    .polarity = NRF_GPIOTE_POLARITY_TOGGLE,
                                    .init_val = NRF_GPIOTE_INITIAL_VALUE_LOW};
  if (nrfx_gpiote_output_configure(&gpiote, CSD_PIN, &output, &task) !=
      NRFX_SUCCESS)
    return -EIO;
  nrfx_gpiote_out_task_enable(&gpiote, CSD_PIN);
  nrfx_gpiote_clr_task_trigger(&gpiote, CSD_PIN);
  if (nrfx_ppi_channel_alloc(&cutoff_channel) != NRFX_SUCCESS)
    return -ENOMEM;
  nrfx_timer_config_t cfg = NRFX_TIMER_DEFAULT_CONFIG(1000000);
  cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
  cfg.interrupt_priority = 6;
  IRQ_CONNECT(TIMER4_IRQn, 6, nrfx_isr, nrfx_timer_4_irq_handler, 0);
  if (nrfx_timer_init(&cutoff, &cfg, cutoff_irq) != NRFX_SUCCESS)
    return -EIO;
  if (nrfx_ppi_channel_assign(
          cutoff_channel, nrfx_timer_compare_event_address_get(&cutoff, 0),
          nrfx_gpiote_clr_task_address_get(&gpiote, CSD_PIN)) != NRFX_SUCCESS)
    return -EIO;
  if (nrfx_ppi_channel_fork_assign(cutoff_channel, 0) != NRFX_SUCCESS)
    return -EIO;
  gpio_init_callback(&dio_callback, dio_irq, BIT(dio.pin));
  if (gpio_add_callback(dio.port, &dio_callback))
    return -EIO;
  /* Configure allocates a dedicated IN event (never the shared PORT event).
   * Capture attach checks its exact endpoint before enabling the PPI route. */
  if (gpio_pin_interrupt_configure_dt(&dio, GPIO_INT_EDGE_TO_ACTIVE))
    return -EIO;
  if (gpio_pin_interrupt_configure_dt(&dio, GPIO_INT_DISABLE))
    return -EIO;
  resources = true;
  return 0;
}
int r2_lora_hw_start(uint32_t gen, bool receive) {
  stage(R2_LORA_STAGE_START);
  atomic_val_t serial = atomic_get(&stop_serial);
  if (!gen)
    return -EINVAL;
  if (atomic_get(&fault))
    return atomic_get(&fault);
  if (atomic_get(&running) || atomic_get(&dma_inflight) || capture_owned)
    return -EBUSY;
  struct r2_capture current;
  if (!r2_capture_now(&current))
    return -EAGAIN;
  int rc = init_resources();
  if (rc)
    return fail(rc);
  rc = r2_capture_lora_attach(
      &dio, nrfx_gpiote_clr_task_address_get(&gpiote, CSD_PIN));
  if (rc)
    return rc == -EAGAIN ? rc : fail(rc);
  capture_owned = true;
  active_epoch = current.epoch;
  receive_mode = receive;
  generation = gen;
  atomic_clear(&latched);
  atomic_clear(&launch_aborted);
  /* Only this protected fence may release reset. stop_begin can preempt any
   * later sleep/SPI; subsequent commands cannot undo its asserted reset. */
  unsigned key = irq_lock();
  if (serial == atomic_get(&stop_serial)) {
    atomic_set(&running, 1);
    atomic_set(&phase, 1);
    rc = gpio_pin_set_dt(&power, 1);
    if (!rc)
      rc = gpio_pin_set_dt(&reset, 0);
  } else
    rc = -ECANCELED;
  irq_unlock(key);
  if (rc)
    return rc == -ECANCELED ? rc : fail(rc);
  k_sleep(K_MSEC(10));
  const uint8_t standby[] = {0}, tcxo[] = {2, 0, 1, 64}, cal[] = {0x7f},
                image[] = {0xe1, 0xe9};
  const uint8_t regulator[] = {1}, type[] = {1}, switching[] = {1},
                bases[] = {0, 0};
  const uint8_t pa[] = {4, 7, 0, 1}, drive[] = {0, 2},
                freq[] = {0x39, 0x30, 0, 0};
  const uint8_t mod[] = {10, 6, 2, 0}, fallback[] = {0x20};
#define CMD(op, a)                                                             \
  do {                                                                         \
    rc = command(op, a, sizeof(a));                                            \
    if (rc)                                                                    \
      return rc == -ECANCELED ? rc : fail(rc);                                 \
  } while (0)
  CMD(0x80, standby);
  CMD(0x97, tcxo);
  CMD(0x89, cal);
  CMD(0x98, image);
  CMD(0x96, regulator);
  CMD(0x8a, type);
  CMD(0x9d, switching);
  CMD(0x8f, bases);
  CMD(0x95, pa);
  CMD(0x8e, drive);
  CMD(0x86, freq);
  CMD(0x8b, mod);
  CMD(0x93, fallback);
#undef CMD
  uint8_t v;
  if (!(rc = reg_write(0x08e7, 0x38)) && !(rc = reg_read(0x08d8, &v)))
    rc = reg_write(0x08d8, v | 0x1e);
  if (!rc && !(rc = reg_read(0x0889, &v)))
    rc = reg_write(0x0889, v & ~4u);
  if (!rc && !(rc = reg_read(0x0736, &v)))
    rc = reg_write(0x0736, v | 4u);
  if (!rc)
    rc = reg_write(0x0740, 0x14);
  if (!rc)
    rc = reg_write(0x0741, 0x24);
  if (!rc)
    rc = reg_read(0x0740, &v);
  if (!rc && v != 0x14)
    rc = -EIO;
  if (!rc)
    rc = reg_read(0x0741, &v);
  if (!rc && v != 0x24)
    rc = -EIO;
  if (rc)
    return rc == -ECANCELED ? rc : fail(rc);
  return receive ? r2_lora_hw_rx_rearm() : 0;
}
static int arm_edge(uint32_t gen, uint8_t new_phase) {
  int rc = gpio_pin_interrupt_configure_dt(&dio, GPIO_INT_DISABLE);
  if (rc)
    return rc;
  /* No inherited high IRQ may be mistaken for a new completed packet, or
   * silently prevent the next rising edge. ClearIrqStatus must really drop
   * DIO1 before binding this operation's generation. */
  rc = gpio_pin_get_dt(&dio);
  if (rc)
    return rc < 0 ? rc : -EIO;
  unsigned key = irq_lock();
  if (!atomic_get(&running))
    rc = -ECANCELED;
  else {
    generation = gen;
    atomic_clear(&latched);
    atomic_set(&phase, new_phase);
  }
  irq_unlock(key);
  if (!rc)
    rc = gpio_pin_interrupt_configure_dt(&dio, GPIO_INT_EDGE_TO_ACTIVE);
  return rc;
}
int r2_lora_hw_rx_rearm(void) {
  stage(R2_LORA_STAGE_REARM);
  if (!receive_mode || atomic_get(&phase) != 1)
    return -EBUSY;
  if (!capture_live())
    return fail(-ESTALE);
  const uint8_t standby[] = {0}, single[] = {0, 0, 0};
  int rc = command(0x80, standby, sizeof(standby));
  if (!rc)
    rc = packet_params(R2_LORA_HW_BYTES);
  if (!rc)
    rc = mask_irq(false);
  if (!rc)
    rc = clear_irq();
  if (!rc)
    rc = arm_edge(generation, 2);
  if (!rc)
    rc = enable_fem(false, 0, 0, NULL);
  if (!rc)
    rc = command(0x82, single, sizeof(single));
  return rc && rc != -ECANCELED ? fail(rc) : rc;
}
uint32_t r2_lora_hw_budget_us(size_t n) {
  if (!n || n > R2_LORA_HW_BYTES)
    return 0;
  return n <= 80 ? TIMING_CUTOFF_US : DIAGNOSTIC_CUTOFF_US;
}
int r2_lora_hw_tx_before(const uint8_t *bytes, size_t n, uint32_t gen,
                       uint64_t latest_finish) {
  uint32_t budget = r2_lora_hw_budget_us(n);
  if (!bytes || !budget || !gen)
    return -EINVAL;
  if (receive_mode || atomic_get(&phase) != 1 || atomic_get(&dma_inflight))
    return -EBUSY;
  if (!capture_live())
    return fail(-ESTALE);
  const uint8_t standby[] = {0};
  int rc = command(0x80, standby, sizeof(standby));
  if (!rc)
    rc = packet_params((uint8_t)n);
  if (!rc)
    rc = mask_irq(true);
  if (!rc)
    rc = clear_irq();
  if (!rc) {
    if (atomic_get(&fault) || atomic_get(&dma_inflight))
      return -EBUSY;
    dma_tx[0] = 0x0e;
    dma_tx[1] = 0;
    memcpy(dma_tx + 2, bytes, n);
    rc = transfer(n + 2);
  }
  if (!rc)
    rc = arm_edge(gen, 4);
  bool deferred = false;
  if (!rc)
    rc = enable_fem(true, budget, latest_finish, &deferred);
  if (deferred) {
    /* Preparation armed a new capture generation, but never enabled RF.
     * Retire that unsubmitted attempt without fault or deadline renewal. */
    atomic_cas(&phase, 4, 1);
    return rc;
  }
  /* SX1262 RTC ticks=15.625us. Timing340ms; diagnostics420ms, within
   * their independent360/450ms hardware FEM cutoffs respectively. */
  const uint8_t timeout[] = {0, n <= 80 ? 0x55 : 0x69, 0};
  if (!rc)
    rc = command(0x83, timeout, sizeof(timeout));
  /* Linearize software retirement against the cutoff IRQ. No SPI, waits or
   * peripheral calls inside this short fence. Before it, an aborted attempt
   * returns EINTR; after phase3 commits, a missing DONE is a real TX timeout. */
  unsigned key = irq_lock();
  bool aborted = atomic_get(&launch_aborted) && !atomic_get(&fault) &&
                 (!rc || rc == -ECANCELED);
  if (!rc && !aborted)
    atomic_cas(&phase, 4, 3);
  irq_unlock(key);
  if (aborted)
    return -EINTR; /* Reset asserted; mandatory closure, never immediate retry. */
  /* Reserve EAGAIN for a healthy scheduling deferral. A peripheral error
   * with that errno must still enter the fault/closure path. */
  if (rc == -EAGAIN)
    rc = -EIO;
  return rc && rc != -ECANCELED ? fail(rc) : rc;
}
int r2_lora_hw_tx(const uint8_t *bytes, size_t n, uint32_t gen) {
  return r2_lora_hw_tx_before(bytes, n, gen, 0);
}
int r2_lora_hw_take(struct r2_lora_hw_event *out) {
  if (atomic_get(&fault))
    return atomic_get(&fault);
  if (!atomic_get(&running))
    return -ECANCELED;
  if (!atomic_get(&latched))
    return atomic_get(&cutoff_expired) ? fail(-ETIMEDOUT) : 0;
  if (!out)
    return -EINVAL;
  const uint8_t dummy[] = {0, 0, 0};
  int rc = command(0x12, dummy, sizeof(dummy));
  if (!rc && !status_ok(dma_rx[1]))
    rc = -EIO;
  uint16_t flags = ((uint16_t)dma_rx[2] << 8) | dma_rx[3];
  if(!rc)wd.irq_status=flags;
  uint8_t state = (uint8_t)atomic_get(&phase);
  if (!rc) {
    *out = (struct r2_lora_hw_event){.tick = edge.tick,
                                     .capture_epoch = edge.epoch,
                                     .local_us = edge_us,
                                     .generation = edge_generation};
    if (state == 3 && flags == 1) {
      out->kind = R2_LORA_HW_TX_DONE;
      count(&counters.tx_done);
    } else if (state == 2 && (flags & 2) && !(flags & 0x260)) {
      rc = command(0x13, dummy, sizeof(dummy));
      if (!rc && !status_ok(dma_rx[1]))
        rc = -EIO;
      uint8_t n = dma_rx[2], offset = dma_rx[3];
      if (!rc && (!n || n > R2_LORA_HW_BYTES))
        rc = -EMSGSIZE;
      if (!rc && n > 256u - offset)
        rc = -EMSGSIZE;
      if (!rc) {
        memset(dma_tx, 0, n + 3);
        dma_tx[0] = 0x1e;
        dma_tx[1] = offset;
        rc = transfer(n + 3);
        if (!rc && !status_ok(dma_rx[2]))
          rc = -EIO;
        if (!rc) {
          out->length = n;
          memcpy(out->frame, dma_rx + 3, n);
          out->kind = R2_LORA_HW_RX_DONE;
          count(&counters.rx_done);
        }
      }
    } else if (state == 2 && (flags & 0x260)) {
      count(&counters.crc_bad);
      rc = -EBADMSG;
    } else
      rc = -EIO;
  }
  /* Copy packet and physical event BEFORE IRQ clear or FIFO reuse/rearm.
   * Bad RF packets are counted/dropped locally; transport errors are latched.
   */
  if (!rc || rc == -EBADMSG || rc == -EMSGSIZE) {
    int dropped = rc;
    rc = clear_irq();
    if (!rc) {
      atomic_clear(&latched);
      atomic_clear(&cutoff_expired);
      atomic_set(&phase, 1);
    }
    if (!rc) {
      if (dropped) {
        rc = r2_lora_hw_rx_rearm();
        return rc ? rc : 0;
      }
      return 1;
    }
  }
  return rc == -ECANCELED ? rc : fail(rc);
}
void r2_lora_hw_stop_begin(uint64_t original_deadline_us) {
  (void)original_deadline_us;
  unsigned key = irq_lock();
  atomic_inc(&stop_serial);
  inhibit();
  irq_unlock(key);
}
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *out) {
  if (!out)
    return;
  bool retired = !atomic_get(&dma_inflight), released = !capture_owned;
  if (!atomic_get(&running) && retired && capture_owned) {
    int rc = gpio_pin_interrupt_configure_dt(&dio, GPIO_INT_DISABLE);
    if (!rc)
      rc = r2_capture_lora_detach();
    if (rc) {
      note_fault(rc,true);
      atomic_cas(&fault, 0, rc);
    } else {
      capture_owned = false;
      released = true;
    }
  }
  /* Pin output registers establish asserted reset and FEM disable. They do
   * not prove RF power or standby. Retained inert TIMER4/GPIOTE allocations
   * can be reused only after this complete closure, never after a fault. */
  bool off = !nrf_gpio_pin_out_read(CSD_PIN) &&
             !nrf_gpio_pin_out_read(NRF_GPIO_PIN_MAP(1, 9)) &&
             !nrf_gpio_pin_out_read(NRF_GPIO_PIN_MAP(0, 30));
  *out = (struct r2_lora_hw_ack){.dma_retired = retired,
                                 .radio_quiescent = !nrf_gpio_pin_out_read(16),
                                 .fem_off = off,
                                 .capture_released = released,
                                 .fault = atomic_get(&fault) != 0};
  wd.closure=(uint32_t)out->dma_retired|((uint32_t)out->radio_quiescent<<1)|
    ((uint32_t)out->fem_off<<2)|((uint32_t)out->capture_released<<3)|((uint32_t)out->fault<<4);
}
void r2_lora_hw_watchdog_trip(int error) {(void)fail(error);}
void r2_lora_hw_diag(struct r2_lora_hw_diag *out) {
  if (!out)
    return;
  unsigned key = irq_lock();
  *out = counters;
  out->error = atomic_get(&fault);
  out->generation = generation;
  out->capture_epoch = (uint32_t)edge.epoch;
  out->phase = (uint8_t)atomic_get(&phase);
  irq_unlock(key);
}
