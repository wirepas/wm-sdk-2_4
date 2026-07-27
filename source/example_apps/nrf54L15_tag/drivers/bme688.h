/* BME688 environmental sensor driver — Bosch SensorAPI wrapper (I2C)
 *
 * Uses the official Bosch BME68x SensorAPI (bme68x_api/bme68x.c) for
 * complete T / P / H compensation and gas resistance (VOC proxy).
 *
 * Gas resistance interpretation (MOx sensor, BME688 variant HIGH):
 *   > 50 kΩ  — good air quality
 *   10–50 kΩ — moderate
 *   < 10 kΩ  — poor (elevated VOC / CO₂ equivalent)
 * For a calibrated VOC Index (0–500 scale), integrate the Bosch BSEC 2.x
 * library (Cortex-M4 FPU binary, compatible with Cortex-M33 nRF54L15).
 *
 * Usage (two-phase, non-blocking):
 *   1. BME688_init(&cfg)                 — once, at boot
 *   2. BME688_trigger()                  — start forced-mode measurement
 *   3. wait BME688_meas_duration_us() µs — scheduler delay
 *   4. BME688_read(&result)              — read compensated values
 */

#ifndef BME688_H_
#define BME688_H_

#include <stdint.h>
#include <stdbool.h>

/* ── Result structure ───────────────────────────────────────────────────────── */
typedef struct
{
    float    temperature;     /* °C                                            */
    float    pressure;        /* Pa                                            */
    float    humidity;        /* %RH                                           */
    float    gas_resistance;  /* Ω  — VOC proxy: higher = cleaner air          */
    bool     gas_valid;       /* true when gas measurement is valid            */
    bool     heat_stable;     /* true when heater temperature was stable       */
} bme688_result_t;

/* ── Configuration ──────────────────────────────────────────────────────────── */
typedef struct
{
    uint8_t i2c_addr;  /* BME688_I2C_ADDR_LOW (0x76) / _HIGH (0x77)           */
    bool    pullup;    /* enable MCU-internal I2C pull-ups                     */
} bme688_cfg_t;

#define BME688_I2C_ADDR_LOW   0x76u
#define BME688_I2C_ADDR_HIGH  0x77u

/* ── Return codes ───────────────────────────────────────────────────────────── */
typedef enum
{
    BME688_RES_OK = 0,
    BME688_RES_I2C_ERR,
    BME688_RES_NOT_INITIALIZED,
    BME688_RES_WRONG_ID,
    BME688_RES_NO_DATA,
    BME688_RES_API_ERR,
} bme688_res_e;

/* ── API ────────────────────────────────────────────────────────────────────── */

/**
 * Initialize I2C, soft-reset the chip, read calibration data.
 * Configures: os_temp=2×, os_pres=1×, os_hum=1×, filter=off,
 *             heater 300 °C / 150 ms (gas/VOC measurement).
 */
bme688_res_e BME688_init(const bme688_cfg_t * cfg);

/**
 * Trigger a single forced-mode measurement (T / P / H / gas).
 * Non-blocking — returns immediately; data is ready after
 * BME688_meas_duration_us() microseconds.
 */
bme688_res_e BME688_trigger(void);

/**
 * Return the total measurement duration in microseconds after a trigger:
 *   T/P/H sampling time + heater duration (150 ms default).
 * Schedule BME688_read() this many µs after BME688_trigger().
 */
uint32_t BME688_meas_duration_us(void);

/**
 * Read and compensate the latest measurement.
 * Call only after the measurement duration has elapsed.
 * Returns BME688_RES_NO_DATA if the chip reports no new data yet.
 */
bme688_res_e BME688_read(bme688_result_t * out);

#endif /* BME688_H_ */
