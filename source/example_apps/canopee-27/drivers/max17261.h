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
    float    temp_c;       /* Temp   — die/thermistor temperature (°C)      */
    int16_t  current_ma;   /* Current — instantaneous current (mA, signed)  */
    uint16_t status;       /* STATUS register (POR/alerts)                  */
} max17261_data_t;

/* Initialise the I2C bus and verify the gauge answers. rsense_mohm is the sense
 * resistor value used to scale Current/Capacity (typ. 5–10 mΩ).
 * pullup enables the MCU internal I2C pull-ups. */
max17261_res_e MAX17261_init(uint16_t rsense_mohm, int pullup);

/* Read a fresh snapshot into *out. */
max17261_res_e MAX17261_read(max17261_data_t * out);

#endif /* MAX17261_H_ */
