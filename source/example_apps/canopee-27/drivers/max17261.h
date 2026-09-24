/* MAX17261 — Maxim ModelGauge m5 single-cell fuel gauge (I2C, address 0x36).
 *
 * Minimal read driver: reads cell voltage, state-of-charge, temperature and
 * current from the always-running gauge. Assumes the part self-initialises with
 * its default model after power-on (no battery-model load here).
 *
 * All MAX1726x registers are 16-bit, little-endian (LSB first on the wire).
 */
#ifndef MAX17261_H_
#define MAX17261_H_

#include <stdint.h>

#define MAX17261_I2C_ADDR   0x36u

typedef enum {
    MAX17261_RES_OK = 0,
    MAX17261_RES_I2C_ERR,
    MAX17261_RES_NOT_INITIALIZED,
} max17261_res_e;

typedef struct {
    uint16_t voltage_mv;   /* VCell  — cell voltage (mV)                    */
    float    soc_pct;      /* RepSOC — reported state of charge (%)         */
    float    temp_c;       /* Temp   — external NTC thermistor temp (°C)    */
    int16_t  current_ma;   /* Current — instantaneous current (mA, signed)  */
    uint16_t status;       /* STATUS register (POR/alerts)                  */
} max17261_data_t;

/* Initialise the I2C bus and verify the gauge answers. rsense_mohm is the sense
 * resistor value used to scale Current/Capacity (typ. 5–10 mΩ).
 * design_cap_mah is the installed pack's nameplate capacity: on a fresh power-up
 * (Status.POR set) it seeds DesignCap / FullCap so Age/Cycles/capacity read
 * meaningfully for THIS pack (pass 0 to leave the gauge defaults untouched).
 * pullup enables the MCU internal I2C pull-ups. */
max17261_res_e MAX17261_init(uint16_t rsense_mohm, uint16_t design_cap_mah, int pullup);

/* Slow-changing battery health / aging metrics (learned by the ModelGauge m5
 * algorithm). Read far less often than the live snapshot — these move over
 * hours/cycles, not seconds. */
typedef struct {
    int16_t  avg_current_ma;   /* AvgCurrent — filtered current (mA, signed)   */
    float    rep_cap_mah;      /* RepCap     — remaining capacity (mAh)        */
    float    full_cap_mah;     /* FullCapRep — learned full capacity (mAh)     */
    float    design_cap_mah;   /* DesignCap  — nameplate capacity (mAh)        */
    float    age_pct;          /* Age = FullCapRep/DesignCap (%) — state-of-health */
    float    cycles;           /* Cycles     — equivalent full charge cycles   */
    uint32_t tte_s;            /* TimeToEmpty (s), 0xFFFF..→ N/A while charging */
    uint32_t ttf_s;            /* TimeToFull  (s), N/A while discharging       */
    float    timer_h;          /* TimerH — total powered lifetime (hours)      */
} max17261_health_t;

/* Read a fresh snapshot into *out. */
max17261_res_e MAX17261_read(max17261_data_t * out);

/* Read the slow battery-health / aging metrics into *out. */
max17261_res_e MAX17261_read_health(max17261_health_t * out);

/* Clear the sticky temperature min/max alert status bits (re-arm the ALRT pin).
 * Call after handling a temperature alert and returning to the safe window. */
void MAX17261_clear_temp_alert(void);

/* Raw 16-bit register read (little-endian) — for diagnostics/tuning. */
max17261_res_e MAX17261_read_reg(uint8_t reg, uint16_t * val);
/* Raw 16-bit register write — for diagnostics/tuning. */
max17261_res_e MAX17261_write_reg(uint8_t reg, uint16_t val);

#endif /* MAX17261_H_ */
