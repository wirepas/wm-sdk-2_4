/**
    @file uar_print.c
    @brief      Handles the UART pint of beacons.
    @copyright  Wirepas Oy 2024
*/


#include <stdlib.h>

#include "api.h"
#include "app_scheduler.h"
#include "measurements.h"

#define DEBUG_LOG_MODULE_NAME "PRINT_DATA"  // Should not be changed as it is used for post processing
#define DEBUG_LOG_MAX_LEVEL   LVL_DEBUG
#include "debug_log.h"

extern measurement_table_t   m_meas_table;  // stores nw/cb data, defined in measurements.c

uint32_t print_network_beacons_list(void)
{
    uint8_t i        = 0;
    uint8_t is_connected;
    uint8_t num_meas = m_meas_table.num_beacons;
    app_addr_t m_addr;

    lib_settings->getNodeAddress(&m_addr);

    if (num_meas > 0)
    {
        /** Display the data */
        LOG(LVL_INFO, "|\tAddress   \t|\tPath loss\t|\tTX Power\t|\tIs connected?\t|\tIs next hop?");
        for (i = 0; i < num_meas; i++)
        {
            if ((m_meas_table.beacons[i].address != m_addr) && (m_meas_table.beacons[i].address != 0))
            {
                /** Filters own address */
                is_connected = m_meas_table.beacons[i].is_connected == 255?0:1;
                LOG(LVL_INFO, "|\t%*u\t|\t\t%d\t|\t\t%d\t|\t\t%d\t|\t\t%d", 10, m_meas_table.beacons[i].address, m_meas_table.beacons[i].path_loss, m_meas_table.beacons[i].txpower,is_connected, m_meas_table.beacons[i].is_next_hop);
                LOG_FLUSH(LVL_DEBUG);
            }
        }
    }
    return APP_SCHEDULER_STOP_TASK;
}
