/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef RADIO_POWER_TABLE_NRF54L15_8DBM_H_
#define RADIO_POWER_TABLE_NRF54L15_8DBM_H_


/** +8 dBm power table for nRF54L15, with 9 power levels.
 *  This power table can be used with the WLCSP package. */
const app_lib_radio_cfg_power_t power_table_nrf54l15_8dBm =
{
    .rx_current     = 21,   // 2.1 mA RX current
    .rx_gain_db     = 0,    // 0 dB RX gain
    .power_count    = 9,    // 9 power levels
    .powers =
    {
        {RADIO_TXPOWER_TXPOWER_Neg40dBm, -40, 1, 29},  //  2.91 mA
        {RADIO_TXPOWER_TXPOWER_Neg20dBm, -20, 1, 38},  //  3.77 mA
        {RADIO_TXPOWER_TXPOWER_Neg16dBm, -16, 1, 39},  //  3.94 mA
        {RADIO_TXPOWER_TXPOWER_Neg12dBm, -12, 1, 42},  //  4.21 mA
        {RADIO_TXPOWER_TXPOWER_Neg8dBm,  -8,  1, 46},  //  4.62 mA
        {RADIO_TXPOWER_TXPOWER_Neg4dBm,  -4,  1, 55},  //  5.47 mA
        {RADIO_TXPOWER_TXPOWER_0dBm,      0,  1, 68},  //  6.78 mA
        {RADIO_TXPOWER_TXPOWER_Pos4dBm,   4,  1, 96},  //  9.55 mA
        {RADIO_TXPOWER_TXPOWER_Pos8dBm,   8,  1, 137}, // 13.70 mA
    },
};

#if defined(RADIO_CUSTOM_POWER_TABLE_H)
__STATIC_INLINE const app_lib_radio_cfg_power_t * get_custom_power_table(void)
{
    return &power_table_nrf54l15_8dBm;
}
#endif  // RADIO_CUSTOM_POWER_TABLE_H

#endif  // RADIO_POWER_TABLE_NRF54L15_8DBM_H_
