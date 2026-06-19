/* hw_delay for nRF54L15 — port of mcu/nrf/common/hal/hw_delay.c
 * Uses NRF_RTC10 (RTC10_IRQn) instead of NRF_RTC0 which doesn't exist on nRF54L15.
 */

#include "mcu.h"
#include "api.h"
#include "hw_delay.h"

#define MINIMUM_TICKS   3u

/* RTC clocked by LFCLK at 32 kHz */
#define LFCLK_FREQ      32768UL
#define RTC_PRESCALER   0UL

#define MAX_RTC_TICK    0x00FFFFFFul
#define MAX_RTC_SEC     (MAX_RTC_TICK / (LFCLK_FREQ * (RTC_PRESCALER + 1UL)))
#define MAX_RTC_USEC    (1000000UL * MAX_RTC_SEC)

/* 32768 Hz → ~30.5 µs per tick → TICK_IN_US = 0 by integer division.
 * Use inverse: ticks_per_second = 32768; ticks = us * 32768 / 1000000. */
#define TICKS_PER_S     (LFCLK_FREQ / (RTC_PRESCALER + 1UL))

static volatile hw_delay_callback_f m_hw_delay_callback;
static bool m_initialized;
static bool m_started;

static void timer_event_disable(void)
{
    NRF_RTC10->EVTENCLR = RTC_EVTENSET_COMPARE0_Msk;
    NRF_RTC10->INTENCLR = RTC_INTENSET_COMPARE0_Msk;
}

static void timer_event_enable(void)
{
    NRF_RTC10->EVTENSET = RTC_EVTENSET_COMPARE0_Msk;
    NRF_RTC10->INTENSET = RTC_INTENSET_COMPARE0_Msk;
}

static void event_clear(volatile uint32_t *p_event)
{
    *p_event = 0;
    volatile uint32_t dummy = *p_event;
    (void)dummy;
}

static hw_delay_res_e rtc_start(void)
{
    if (m_started || !m_initialized)
    {
        return HW_DELAY_ERR;
    }
    NRF_RTC10->TASKS_START = 1;
    m_started = true;
    return HW_DELAY_OK;
}

static uint32_t convert_us_to_tick(uint32_t time_us)
{
    /* ticks = us * 32768 / 1000000 — use 64-bit to avoid overflow */
    uint32_t ticks = (uint32_t)(((uint64_t)time_us * TICKS_PER_S) / 1000000UL);
    return MAX_RTC_TICK & ticks;
}

static void setup_trigger_in_tick(uint32_t time_tick)
{
    if (time_tick < MINIMUM_TICKS)
    {
        time_tick = MINIMUM_TICKS;
    }
    uint32_t event = NRF_RTC10->COUNTER + time_tick;
    rtc_start();
    timer_event_enable();
    NRF_RTC10->CC[0] = event;
}

static void setup_trigger_in_us(uint32_t time_us)
{
    setup_trigger_in_tick(convert_us_to_tick(time_us));
}

static void IRQHandler(void)
{
    if ((NRF_RTC10->EVENTS_COMPARE[0] != 0) &&
        ((NRF_RTC10->INTENSET & RTC_INTENSET_COMPARE0_Msk) != 0))
    {
        event_clear(&NRF_RTC10->EVENTS_COMPARE[0]);

        hw_delay_callback_f cb = m_hw_delay_callback;
        if (cb)
        {
            uint32_t next_us = cb();
            if (next_us > 0u && next_us < MAX_RTC_USEC)
            {
                setup_trigger_in_us(next_us);
                return;
            }
        }

        timer_event_disable();
        hw_delay_cancel();
    }
}

hw_delay_res_e hw_delay_init(void)
{
    if (m_initialized)
    {
        return HW_DELAY_ERR;
    }

    m_hw_delay_callback = (hw_delay_callback_f)NULL;

    NRF_RTC10->TASKS_STOP  = 1;
    NRF_RTC10->TASKS_CLEAR = 1;
    NRF_RTC10->CC[0]       = 0;
    event_clear(&NRF_RTC10->EVENTS_COMPARE[0]);

    NRF_RTC10->PRESCALER = RTC_PRESCALER;
    NRF_RTC10->INTENCLR  = 0xFFFFFFFFu;

    lib_system->clearPendingFastAppIrq(RTC10_IRQn);
    lib_system->enableAppIrq(true, RTC10_IRQn, APP_LIB_SYSTEM_IRQ_PRIO_LO, IRQHandler);

    m_initialized = true;
    m_started     = false;

    return HW_DELAY_OK;
}

hw_delay_res_e hw_delay_trigger_us(hw_delay_callback_f callback, uint32_t time_us)
{
    if (!m_initialized)
    {
        return HW_DELAY_ERR;
    }
    if (time_us > MAX_RTC_USEC || callback == NULL)
    {
        return HW_DELAY_PARAM_ERR;
    }

    m_hw_delay_callback = callback;
    setup_trigger_in_us(time_us);

    return HW_DELAY_OK;
}

hw_delay_res_e hw_delay_cancel(void)
{
    if (m_hw_delay_callback == NULL)
    {
        return HW_DELAY_NOT_TRIGGERED;
    }
    if (!m_initialized)
    {
        return HW_DELAY_ERR;
    }

    NRF_RTC10->TASKS_STOP       = 1;
    NRF_RTC10->EVENTS_COMPARE[0] = 0;
    m_started               = false;
    m_hw_delay_callback     = NULL;

    return HW_DELAY_OK;
}
