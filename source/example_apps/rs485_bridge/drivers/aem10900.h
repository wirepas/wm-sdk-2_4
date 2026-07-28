/* AEM10900 PMIC driver — e-peas energy harvesting IC
 *
 * Interface : I2C, address 0x2D (fixed)
 * Register map sourced from: github.com/trembel/aem10900
 */

#ifndef AEM10900_H_
#define AEM10900_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Register addresses ─────────────────────────────────────────────────────── */
#define AEM10900_REG_VERSION    0x00u   /* R   — major[7:4] / minor[3:0]        */
#define AEM10900_REG_MPPTCFG   0x01u   /* R/W — MPPT ratio[3:0] + timing[6:4] */
#define AEM10900_REG_VOVDIS    0x02u   /* R/W — overdischarge threshold [5:0]  */
#define AEM10900_REG_VOVCH     0x03u   /* R/W — overcharge threshold [5:0]     */
#define AEM10900_REG_TEMPCOLD  0x04u   /* R/W — cold temp threshold [7:0]      */
#define AEM10900_REG_TEMPHOT   0x05u   /* R/W — hot temp threshold [7:0]       */
#define AEM10900_REG_PWR       0x06u   /* R/W — power mode                     */
#define AEM10900_REG_SLEEP     0x07u   /* R/W — sleep mode                     */
#define AEM10900_REG_STOMON    0x08u   /* R/W — storage monitor rate           */
#define AEM10900_REG_APM       0x09u   /* R/W — active power measurement       */
#define AEM10900_REG_IRQEN     0x0Au   /* R/W — interrupt enables              */
#define AEM10900_REG_CTRL      0x0Bu   /* R/W — config source (I2C vs GPIO)    */
#define AEM10900_REG_IRQFLG    0x0Cu   /* R   — interrupt flags (clear on read)*/
#define AEM10900_REG_STATUS    0x0Du   /* R   — status                         */
#define AEM10900_REG_APM0      0x0Eu   /* R   — APM data byte 0 (low)          */
#define AEM10900_REG_APM1      0x0Fu   /* R   — APM data byte 1 (mid)          */
#define AEM10900_REG_APM2      0x10u   /* R   — APM data byte 2 (high+shift)   */
#define AEM10900_REG_TEMP      0x11u   /* R   — temperature measurement        */
#define AEM10900_REG_STO       0x12u   /* R   — storage/battery voltage        */
#define AEM10900_REG_SRC       0x13u   /* R   — source voltage (lookup table)  */

/* ── MPPTCFG enums ──────────────────────────────────────────────────────────── */
typedef enum {
    AEM10900_MPPT_RATIO_ZMPP = 0x00u,  /* zero MPP (no tracking) */
    AEM10900_MPPT_RATIO_90   = 0x01u,
    AEM10900_MPPT_RATIO_65   = 0x02u,
    AEM10900_MPPT_RATIO_60   = 0x03u,
    AEM10900_MPPT_RATIO_85   = 0x04u,
    AEM10900_MPPT_RATIO_75   = 0x05u,
    AEM10900_MPPT_RATIO_70   = 0x06u,
    AEM10900_MPPT_RATIO_80   = 0x07u,
    AEM10900_MPPT_RATIO_35   = 0x08u,
    AEM10900_MPPT_RATIO_50   = 0x09u,
} aem10900_mppt_ratio_e;

typedef enum {
    AEM10900_MPPT_TIM_2MS_64MS      = 0x00u,
    AEM10900_MPPT_TIM_256MS_16384MS = 0x01u,
    AEM10900_MPPT_TIM_64MS_4096MS   = 0x02u,
    AEM10900_MPPT_TIM_8MS_1024MS    = 0x03u,
    AEM10900_MPPT_TIM_4MS_256MS     = 0x04u,
    AEM10900_MPPT_TIM_2MS_128MS     = 0x05u,
    AEM10900_MPPT_TIM_4MS_512MS     = 0x06u,
    AEM10900_MPPT_TIM_2MS_256MS     = 0x07u,
} aem10900_mppt_timing_e;

/* ── PWR register bits [0x06] ───────────────────────────────────────────────── */
#define AEM10900_PWR_KEEPALEN   (1u << 0)   /* keep-alive output enable        */
#define AEM10900_PWR_HPEN       (1u << 1)   /* high-power mode                 */
#define AEM10900_PWR_TMONEN     (1u << 2)   /* temperature monitoring enable   */
#define AEM10900_PWR_STOCHDIS   (1u << 3)   /* storage charging disable        */

/* ── SLEEP register [0x07] ─────────────────────────────────────────────────── */
#define AEM10900_SLEEP_EN       (1u << 0)

typedef enum {
    AEM10900_SLEEP_SRC_DISABLED = 0x00u,
    AEM10900_SLEEP_SRC_202MV    = 0x01u,
    AEM10900_SLEEP_SRC_255MV    = 0x02u,
    AEM10900_SLEEP_SRC_300MV    = 0x03u,
    AEM10900_SLEEP_SRC_360MV    = 0x04u,
    AEM10900_SLEEP_SRC_405MV    = 0x05u,
    AEM10900_SLEEP_SRC_510MV    = 0x06u,
    AEM10900_SLEEP_SRC_600MV    = 0x07u,
} aem10900_sleep_srcthresh_e;

/* ── STOMON register [0x08] ─────────────────────────────────────────────────── */
typedef enum {
    AEM10900_STOMON_RATE_1024MS = 0x00u,
    AEM10900_STOMON_RATE_512MS  = 0x01u,
    AEM10900_STOMON_RATE_256MS  = 0x02u,
    AEM10900_STOMON_RATE_128MS  = 0x03u,
    AEM10900_STOMON_RATE_64MS   = 0x04u,
} aem10900_stomon_rate_e;

/* ── APM register [0x09] ────────────────────────────────────────────────────── */
#define AEM10900_APM_EN         (1u << 0)

typedef enum {
    AEM10900_APM_MODE_PULSE = 0x00u,    /* pulse counter */
    AEM10900_APM_MODE_POWER = 0x01u,    /* power meter   */
} aem10900_apm_mode_e;

typedef enum {
    AEM10900_APM_WIN_128MS_256MS = 0x00u,
    AEM10900_APM_WIN_64MS_128MS  = 0x01u,
    AEM10900_APM_WIN_32MS_64MS   = 0x02u,
} aem10900_apm_window_e;

/* ── IRQEN / IRQFLG bits [0x0A / 0x0C] ─────────────────────────────────────── */
#define AEM10900_IRQ_I2CRDY     (1u << 0)
#define AEM10900_IRQ_VOVDIS     (1u << 1)
#define AEM10900_IRQ_VOVCH      (1u << 2)
#define AEM10900_IRQ_SRCTHRESH  (1u << 3)
#define AEM10900_IRQ_TEMP       (1u << 4)
#define AEM10900_IRQ_APMDONE    (1u << 5)
#define AEM10900_IRQ_APMERR     (1u << 6)

/* ── CTRL register [0x0B] ───────────────────────────────────────────────────── */
#define AEM10900_CTRL_UPDATE_GPIO   0x00u   /* config driven by GPIO pins   */
#define AEM10900_CTRL_UPDATE_I2C    0x01u   /* config driven by I2C regs    */
#define AEM10900_CTRL_SYNCBUSY      (1u << 2) /* set while writing to EEPROM */

/* ── STATUS register bits [0x0D] ────────────────────────────────────────────── */
#define AEM10900_STATUS_VOVDIS      (1u << 1)
#define AEM10900_STATUS_VOVCH       (1u << 2)
#define AEM10900_STATUS_SRCTHRESH   (1u << 3)
#define AEM10900_STATUS_TEMP        (1u << 4)
#define AEM10900_STATUS_CHARGE      (1u << 6)   /* storage element charging */
#define AEM10900_STATUS_BSTDIS      (1u << 7)   /* boost converter disabled */

/* ── Voltage threshold encoding ─────────────────────────────────────────────── */
/*
 * VOVCH  : V = 1.2375 + THRESH × 0.05625  (overcharge, stops charging above)
 * VOVDIS : V = 0.50625 + THRESH × 0.05625 (overdischarge, disables output below)
 *
 * Inverse (float arithmetic — compute at init time, not in ISR):
 */
#define AEM10900_VOVCH_TO_RAW(v_f)  ((uint8_t)(((v_f) - 1.2375f)  / 0.05625f))
#define AEM10900_VOVDIS_TO_RAW(v_f) ((uint8_t)(((v_f) - 0.50625f) / 0.05625f))

/*
 * Li-ion cell target voltages:
 *   VOVCH  = 4.20 V → raw 53 → actual 4.219 V
 *   VOVDIS = 3.00 V → raw 44 → actual 2.981 V
 */
#define AEM10900_LIION_VOVCH_RAW    53u     /* ≈ 4.22 V — overcharge cutoff  */
#define AEM10900_LIION_VOVDIS_RAW   44u     /* ≈ 2.98 V — deep-discharge limit */

/*
 * Temperature thresholds — ratiometric NTC encoding:
 *   THRESH = 256 × R_NTC(T) / (R_NTC(T) + R_DIV)
 *   R_NTC(T) = R25 × exp(B × (1/T_K − 1/298.15))
 *
 * Board: NCP15XH103J03RC (Murata), R25 = 10 kΩ, B25/50 = 3380 K, R_DIV = 15 kΩ
 *   0°C  → R_NTC = 28 228 Ω → THRESH = 167   (TEMPCOLD: stop charging below 0°C)
 *   45°C → R_NTC =  4 901 Ω → THRESH =  63   (TEMPHOT:  stop charging above 45°C)
 */
#define AEM10900_LIION_TEMPCOLD_RAW     167u    /* 0°C  charging lower limit */
#define AEM10900_LIION_TEMPHOT_RAW       63u    /* 45°C charging upper limit */

/* ── Configuration structure ────────────────────────────────────────────────── */
typedef struct
{
    aem10900_mppt_ratio_e       mppt_ratio;
    aem10900_mppt_timing_e      mppt_timing;

    uint8_t vovdis_raw;         /* 6-bit overdischarge threshold  */
    uint8_t vovch_raw;          /* 6-bit overcharge threshold     */
    uint8_t tempcold_raw;       /* 8-bit cold temperature limit   */
    uint8_t temphot_raw;        /* 8-bit hot temperature limit    */

    uint8_t pwr_mask;           /* AEM10900_PWR_x bits            */

    bool                        sleep_enable;
    aem10900_sleep_srcthresh_e  sleep_srcthresh;

    aem10900_stomon_rate_e      stomon_rate;

    bool                        apm_enable;
    aem10900_apm_mode_e         apm_mode;
    aem10900_apm_window_e       apm_window;

    uint8_t irq_mask;           /* AEM10900_IRQ_x bits            */
} aem10900_cfg_t;

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    AEM10900_RES_OK = 0,
    AEM10900_RES_I2C_ERR,
    AEM10900_RES_NOT_INITIALIZED,
    AEM10900_RES_SYNC_TIMEOUT,
} aem10900_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/**
 * Initialize I2C, write all config registers (0x01–0x0B), wait for
 * SYNCBUSY to clear. Must be called once before any other function.
 */
aem10900_res_e AEM10900_init(const aem10900_cfg_t * cfg);

/** Read the STATUS register [0x0D]. */
aem10900_res_e AEM10900_get_status(uint8_t * status);

/** Read and clear the interrupt flags [0x0C]. */
aem10900_res_e AEM10900_get_irqflg(uint8_t * flags);

/**
 * Read storage (battery) voltage.
 * Conversion: V = 4.8 × raw / 256
 */
aem10900_res_e AEM10900_read_storage_voltage(float * voltage_v);

/**
 * Read source (harvester) voltage.
 * Decoded via internal lookup table (see datasheet Table 5).
 */
aem10900_res_e AEM10900_read_source_voltage(float * voltage_v);

/**
 * Read APM result (power meter or pulse counter).
 * In POWER_METER mode: result_uW = (data << shift) × 0.10166
 * where shift = APM2[7:4], data is the 20-bit value in APM0-APM2[3:0].
 */
aem10900_res_e AEM10900_read_apm(float * result, uint8_t raw_out[3]);

/** Low-level single register read. */
aem10900_res_e AEM10900_read_reg(uint8_t reg, uint8_t * val);

/** Low-level single register write. */
aem10900_res_e AEM10900_write_reg(uint8_t reg, uint8_t val);

/** Return true if the storage element is currently charging. */
bool AEM10900_is_charging(void);

/* ── Ready-to-use Li-ion default configuration ──────────────────────────────── */
/*
 * Suitable for a single Li-ion cell (3.0 V nominal, 4.2 V max, 3.0 V cutoff).
 * - MPPT ratio 80 % — good starting point for a solar cell
 * - MPPT timing 4 ms / 256 ms — VOC sample 4 ms, tracking period 256 ms
 * - Overcharge  : 4.22 V (code 53)
 * - Overdischarge : 2.98 V (code 44)
 * - Temperature monitoring via NCP15XH103J03RC + 22 kΩ divider
 *     cold limit 0°C (code 144), hot limit 45°C (code 47)
 * - Storage sampled every 256 ms
 * - Interrupts on overcharge, overdischarge, and temperature events
 */
#define AEM10900_LIION_DEFAULT_CFG                          \
{                                                           \
    .mppt_ratio      = AEM10900_MPPT_RATIO_80,             \
    .mppt_timing     = AEM10900_MPPT_TIM_4MS_256MS,        \
    .vovch_raw       = AEM10900_LIION_VOVCH_RAW,           \
    .vovdis_raw      = AEM10900_LIION_VOVDIS_RAW,          \
    .tempcold_raw    = AEM10900_LIION_TEMPCOLD_RAW,        \
    .temphot_raw     = AEM10900_LIION_TEMPHOT_RAW,         \
    .pwr_mask        = AEM10900_PWR_KEEPALEN                \
                     | AEM10900_PWR_HPEN                    \
                     | AEM10900_PWR_TMONEN,                 \
    .sleep_enable    = true,                                \
    .sleep_srcthresh = AEM10900_SLEEP_SRC_300MV,           \
    .stomon_rate     = AEM10900_STOMON_RATE_256MS,         \
    .apm_enable      = true,                               \
    .apm_mode        = AEM10900_APM_MODE_POWER,            \
    .apm_window      = AEM10900_APM_WIN_128MS_256MS,       \
    .irq_mask        = AEM10900_IRQ_VOVCH                  \
                     | AEM10900_IRQ_VOVDIS                 \
                     | AEM10900_IRQ_TEMP                   \
                     | AEM10900_IRQ_APMDONE                \
                     | AEM10900_IRQ_APMERR                 \
                     | AEM10900_IRQ_I2CRDY,                \
}

#endif /* AEM10900_H_ */
