/* Davis Instruments 6410/6415 anemometer + wind vane.
 *
 *   Wind speed : reed switch, one closure per revolution. Davis spec — 1 Hz
 *                (1 rev/s) = 2.25 mph = 1.00584 m/s. Pulses are counted on a GPIO
 *                interrupt (debounced) and converted over a sampling window.
 *   Wind vane  : 0..~20 kΩ potentiometer, wiper ratiometric to its supply. Read
 *                on the SAADC (P0.02) against VDD (the pot supply) → 0..360°.
 *
 * The GPIO interrupt handler must call Davis6410_on_pulse(); the pot supply GPIO
 * is switched on only during a direction read (energy saving).
 */
#ifndef DAVIS6410_H_
#define DAVIS6410_H_

#include <stdint.h>
#include <stdbool.h>

/* Initialise the SAADC path and remember the pot-power GPIO id (already set up
 * as an output by the app). */
void Davis6410_init(uint8_t pot_pwr_gpio_id);

/* Call from the wind-speed GPIO interrupt (falling edge). Debounced internally. */
void Davis6410_on_pulse(void);

/* Instantaneous wind speed (m/s): consumes the pulses accumulated since the last
 * call and divides by window_ms. Resets the counter. */
float Davis6410_sample_speed_ms(uint32_t window_ms);

/* Wind direction (0..360°): powers the pot, samples the wiper ratiometrically to
 * VDD, powers the pot off. Returns <0 on SAADC error. */
float Davis6410_read_direction_deg(void);

#endif /* DAVIS6410_H_ */
