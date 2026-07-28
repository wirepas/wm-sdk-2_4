/* Copyright 2024 licensed under Apache License, Version 2.0
 *
 * On the nRF9160 DK, the nRF interface pins (IF0-IF8) between the nRF9160
 * and this companion nRF52840 are not direct PCB traces but go through
 * analog switches, normally enabled by the board controller firmware this
 * app replaces. Our UART (see board.h) uses IF0-IF3, which spans both
 * switch banks (nrf9160dk_nrf52840.dts: switch-nrf-if0-2-ctrl on P0.13,
 * switch-nrf-if3-5-ctrl on P0.24, both active-high) - enable both here,
 * before Libraries_init()/App_init() bring up the UART.
 *
 * Same story for the DK's onboard external flash (MX25R6435F, used by the
 * nRF9160 side as a bulk_q overflow buffer): its SPI/HOLD/WP lines also run
 * through an analog switch (switch-ext-mem-ctrl, EXT_MEM_CTRL, P0.19,
 * active-high - see nrf9160dk_nrf52840_0_14_0.overlay), enabled by default
 * in the stock board controller firmware. Without this, the nRF9160's SPI
 * bus is electrically fine but never reaches the flash chip at all - JEDEC
 * ID probe reads a clean 00 00 00 no matter what CS/HOLD/WP wiring is tried
 * on the nRF9160 side, which is exactly what was observed and briefly (and
 * wrongly) attributed to the CS pin (P0.25/TRACEDATA[3]) instead.
 */
#include "mcu.h"

#define IF0_2_SWITCH_ENABLE_PIN 13 /* P0.13 */
#define IF3_5_SWITCH_ENABLE_PIN 24 /* P0.24 */
#define EXT_MEM_SWITCH_ENABLE_PIN 19 /* P0.19 */

void Board_custom_init(void)
{
    nrf_gpio_cfg_output(IF0_2_SWITCH_ENABLE_PIN);
    nrf_gpio_pin_set(IF0_2_SWITCH_ENABLE_PIN);

    nrf_gpio_cfg_output(IF3_5_SWITCH_ENABLE_PIN);
    nrf_gpio_pin_set(IF3_5_SWITCH_ENABLE_PIN);

    nrf_gpio_cfg_output(EXT_MEM_SWITCH_ENABLE_PIN);
    nrf_gpio_pin_set(EXT_MEM_SWITCH_ENABLE_PIN);
}
