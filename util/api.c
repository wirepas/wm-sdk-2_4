/* Copyright 2017 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "api.h"

// Ensure sizes of enums. If these asserts fail, you should ensure that
// -fshort-enums compilation flag is enabled
// Ensuring every enum. Bit excessive but does not harm either
_Static_assert(sizeof(app_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_special_addr_e) == sizeof(uint32_t));
_Static_assert(sizeof(app_lib_data_qos_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_send_flags_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_send_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_receive_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_app_config_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_fragmented_mode_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_data_config_data_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_hardware_activable_peripheral_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_joining_rx_status_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_mem_area_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_mem_area_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_otap_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_otap_status_e) == sizeof(uint32_t));
_Static_assert(sizeof(app_lib_otap_write_res_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_otap_action_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_radio_cfg_femcmd_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_settings_role_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_nbor_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_diradv_support_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_scan_nbors_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_stack_state_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_route_state_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_beacon_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(install_quality_error_code_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_state_scan_type_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_stack_event_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_system_hardware_magic_e) == sizeof(uint8_t));
_Static_assert(sizeof(app_lib_system_protocol_profile_e) == sizeof(uint8_t));

/* All the libraries */
const app_global_functions_t *  global_func;
const app_lib_data_t *          lib_data;
const app_lib_otap_t *          lib_otap;
const app_lib_settings_t *      lib_settings;
const app_lib_state_t *         lib_state;
const app_lib_storage_t *       lib_storage;
const app_lib_system_t *        lib_system;
const app_lib_time_t *          lib_time;
const app_lib_hardware_t *      lib_hw;
const app_lib_beacon_tx_t *     lib_beacon_tx;
const app_lib_beacon_rx_t *     lib_beacon_rx;
const app_lib_sleep_t *         lib_sleep;
const app_lib_advertiser_t *    lib_advertiser;
const app_lib_joining_t *       lib_joining;
const app_lib_memory_area_t *   lib_memory_area;
const app_lib_radio_cfg_t *     lib_radio_cfg;


const app_lib_secure_storage_t *       lib_secure_storage;

static const void * open_lib_with_fallback(uint32_t name,
                                           uint32_t version)
{
    const void * lib_handle;
    lib_handle = global_func->openLibrary(name, version);
    if (lib_handle != NULL)
    {
        return lib_handle;
    }

    // The requested version is not available.
    // It means that the app is built for a newer stack
    // Try to open older lib to limit the consequences.
    // If new features or modified services are not use,
    // it "could" work, but no guarantees.
    // This fallback allows to reduce the risk of
    // bricking the device
    while (--version >= 0x200)
    {
        lib_handle = global_func->openLibrary(name, version);
        if (lib_handle != NULL)
        {
            return lib_handle;
        }
    }

    return NULL;
}

bool API_Open(const app_global_functions_t * functions)
{
    // The root library
    global_func = functions;

    // Open the libraries
    lib_data = open_lib_with_fallback(APP_LIB_DATA_NAME,
                                      APP_LIB_DATA_VERSION);

    lib_settings = open_lib_with_fallback(APP_LIB_SETTINGS_NAME,
                                          APP_LIB_SETTINGS_VERSION);

    lib_state = open_lib_with_fallback(APP_LIB_STATE_NAME,
                                       APP_LIB_STATE_VERSION);

    lib_system = open_lib_with_fallback(APP_LIB_SYSTEM_NAME,
                                        APP_LIB_SYSTEM_VERSION);

    lib_time = open_lib_with_fallback(APP_LIB_TIME_NAME,
                                      APP_LIB_TIME_VERSION);

    lib_hw = open_lib_with_fallback(APP_LIB_HARDWARE_NAME,
                                    APP_LIB_HARDWARE_VERSION);

    lib_storage = open_lib_with_fallback(APP_LIB_STORAGE_NAME,
                                         APP_LIB_STORAGE_VERSION);

    lib_otap = open_lib_with_fallback(APP_LIB_OTAP_NAME,
                                      APP_LIB_OTAP_VERSION);


    lib_beacon_tx = open_lib_with_fallback(APP_LIB_BEACON_TX_NAME,
                                           APP_LIB_BEACON_TX_VERSION);

    lib_beacon_rx = open_lib_with_fallback(APP_LIB_BEACON_RX_NAME,
                                           APP_LIB_BEACON_RX_VERSION);

    lib_sleep = open_lib_with_fallback(APP_LIB_LONGSLEEP_NAME,
                                       APP_LIB_LONGSLEEP_VERSION);

    lib_advertiser = open_lib_with_fallback(APP_LIB_ADVERTISER_NAME,
                                            APP_LIB_ADVERTISER_VERSION);

    lib_joining = open_lib_with_fallback(APP_LIB_JOINING_NAME,
                                         APP_LIB_JOINING_VERSION);

    lib_memory_area = open_lib_with_fallback(APP_LIB_MEMORY_AREA_NAME,
                                             APP_LIB_MEMORY_AREA_VERSION);

    lib_radio_cfg = open_lib_with_fallback(APP_LIB_RADIO_CFG_NAME,
                                           APP_LIB_RADIO_CFG_VERSION);


    lib_secure_storage = open_lib_with_fallback(APP_LIB_SECURE_STORAGE_NAME,
                                         APP_LIB_SECURE_STORAGE_VERSION);

    // Currently the status is a success if libs present on all platform are opened
    // TODO: add an api.mk / config file that tells which libraries are to be
    // opened and if a library fails to open if it should be handled as an
    // error or not
    return (lib_data
            && lib_settings
            && lib_state
            && lib_system
            && lib_time
            && lib_hw
            && lib_storage
            && lib_otap
            && lib_beacon_tx
            && lib_beacon_rx
            && lib_sleep
            && lib_joining
            && lib_radio_cfg
            && lib_memory_area
            && lib_secure_storage
            );
}
