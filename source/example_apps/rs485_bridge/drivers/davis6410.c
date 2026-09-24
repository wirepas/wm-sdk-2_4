/* Davis 6410/6415 anemometer + wind vane driver — see davis6410.h */

#include <stddef.h>

#include "davis6410.h"
#include "api.h"          /* lib_time */
#include "gpio.h"
#include "board.h"
#include "nrf.h"

/* ── Wind-speed conversion ─────────────────────────────────────────────────
 * Davis: 1 closure/s (1 Hz) = 2.25 mph = 1.00584 m/s. */
#define DAVIS_MS_PER_HZ        1.00584f
/* Reject reed-switch bounce: max wind ~90 Hz (11 ms) so 1 ms is safe. */
#define DAVIS_DEBOUNCE_US      1000u

/* ── SAADC (single-ended, internal 0.9 V ref, gain 2/8 → 3600 mV full scale) ── */
#define SAADC_VREF_MV          900u
#define SAADC_INV_GAIN         4u        /* 1/(2/8) */
#define SAADC_MAX_ADC          1023u     /* 10-bit  */
#define SAADC_TACQ             79u       /* (79+1)*125 ns = 10 µs */

/* PSELP for the wind-vane wiper on P0.02 (external analog input). */
#define PSELP_WIND_DIR \
    ((SAADC_CH_PSELP_CONNECT_AnalogInput << SAADC_CH_PSELP_CONNECT_Pos) \
     | ((uint32_t)BOARD_WIND_DIR_AIN_PORT << SAADC_CH_PSELP_PORT_Pos)   \
     | ((uint32_t)BOARD_WIND_DIR_AIN_PIN  << SAADC_CH_PSELP_PIN_Pos))
/* PSELP for VDD (the pot supply), used as the ratiometric reference. */
#define PSELP_VDD \
    ((SAADC_CH_PSELP_CONNECT_Internal << SAADC_CH_PSELP_CONNECT_Pos) \
     | (SAADC_CH_PSELP_INTERNAL_Vdd   << SAADC_CH_PSELP_INTERNAL_Pos))

static volatile uint32_t              m_pulses = 0;
static app_lib_time_timestamp_hp_t    m_last_pulse;
static uint8_t                        m_pot_pwr_id;

/* Busy-wait a few µs using the high-precision timer (pot settling). */
static void delay_us(uint32_t us)
{
    app_lib_time_timestamp_hp_t end =
        lib_time->addUsToHpTimestamp(lib_time->getTimestampHp(), us);
    while (lib_time->isHpTimestampBefore(lib_time->getTimestampHp(), end)) { }
}

/* One-shot SAADC conversion on the given PSELP → millivolts. */
static uint16_t saadc_read_mv(uint32_t pselp)
{
    volatile uint32_t adc_result = 0;

    for (uint8_t i = 0; i < 8; i++)
        NRF_SAADC->CH[i].PSELP = SAADC_CH_PSELP_CONNECT_NC << SAADC_CH_PSELP_CONNECT_Pos;

    NRF_SAADC->RESULT.PTR    = (uint32_t)&adc_result;
    NRF_SAADC->RESULT.MAXCNT = 2u;   /* one 16-bit sample */
    NRF_SAADC->RESOLUTION    = SAADC_RESOLUTION_VAL_10bit << SAADC_RESOLUTION_VAL_Pos;
    NRF_SAADC->OVERSAMPLE    = SAADC_OVERSAMPLE_OVERSAMPLE_Bypass << SAADC_OVERSAMPLE_OVERSAMPLE_Pos;

    NRF_SAADC->CH[0].CONFIG =
          (SAADC_CH_CONFIG_MODE_SE        << SAADC_CH_CONFIG_MODE_Pos)
        | (SAADC_TACQ                     << SAADC_CH_CONFIG_TACQ_Pos)
        | (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos)
        | (SAADC_CH_CONFIG_GAIN_Gain2_8   << SAADC_CH_CONFIG_GAIN_Pos);
    NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_CONNECT_NC << SAADC_CH_PSELN_CONNECT_Pos;
    NRF_SAADC->CH[0].PSELP = pselp;

    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos;

    NRF_SAADC->EVENTS_STARTED = 0;
    NRF_SAADC->TASKS_START    = 1;
    while (!NRF_SAADC->EVENTS_STARTED) { }

    NRF_SAADC->EVENTS_END   = 0;
    NRF_SAADC->TASKS_SAMPLE = 1;
    while (!NRF_SAADC->EVENTS_END) { }

    NRF_SAADC->ENABLE     = SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos;
    NRF_SAADC->TASKS_STOP = 1;

    uint32_t mv = (adc_result & 0xFFFFu) * (SAADC_VREF_MV * SAADC_INV_GAIN);
    mv /= SAADC_MAX_ADC;
    return (uint16_t)mv;
}

void Davis6410_init(uint8_t pot_pwr_gpio_id)
{
    m_pot_pwr_id = pot_pwr_gpio_id;
    m_pulses     = 0;
    m_last_pulse = lib_time->getTimestampHp();
}

void Davis6410_on_pulse(void)
{
    app_lib_time_timestamp_hp_t now = lib_time->getTimestampHp();
    if (lib_time->getTimeDiffUs(now, m_last_pulse) >= DAVIS_DEBOUNCE_US)
    {
        m_pulses++;
        m_last_pulse = now;
    }
}

float Davis6410_sample_speed_ms(uint32_t window_ms)
{
    if (window_ms == 0u) return 0.0f;
    uint32_t n = m_pulses;   /* single 32-bit read is atomic on M33 */
    m_pulses = 0;
    return ((float)n * DAVIS_MS_PER_HZ * 1000.0f) / (float)window_ms;
}

float Davis6410_read_direction_deg(void)
{
    /* Power the pot, let it settle, sample wiper + VDD, power off. */
    Gpio_outputWrite(m_pot_pwr_id, GPIO_LEVEL_HIGH);
    delay_us(2000u);
    uint16_t wiper_mv = saadc_read_mv(PSELP_WIND_DIR);
    uint16_t vdd_mv   = saadc_read_mv(PSELP_VDD);
    Gpio_outputWrite(m_pot_pwr_id, GPIO_LEVEL_LOW);

    if (vdd_mv == 0u) return -1.0f;
    float deg = ((float)wiper_mv / (float)vdd_mv) * 360.0f;
    if (deg < 0.0f)   deg = 0.0f;
    if (deg >= 360.0f) deg -= 360.0f;
    return deg;
}
