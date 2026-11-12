/* Copyright 2026 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/*
 * \file    app.c
 * \brief   Wirepas Mesh stack provisioning example app
 * \note    To configure initial node parameters:
 * \code{.sh}
 * tools/app_setup.py                                                       \
 *     --hexfile build/<target_board>/<app_name>/final_image_<app_name>.hex \
 *     --device                 <device, e.g., nRF52840>                    \
 *     --nodeAddress            12345                                       \
 *     --networkAddress         0xABCDE                                     \
 *     --networkChannel         5                                           \
 *     --nodeRole               LL_SUBNODE                                  \
 *     --networkAuthKey         <Network auth key (dummy), 32 hex chars>    \
 *     --networkEncKey          <Network encrypt key (dummy), 32 hex chars> \
 *     --provisionMethod        EUID                                        \
 *     --provisionAuthKey       <Provisioning auth key, 32 hex chars>       \
 *     --provisionEncKey        <Provisioning encrypt key, 32 hex chars>    \
 *     --provisionRetries       5                                           \
 *     --provisionTimeout       30                                          \
 *     --authenticatorUidType   1                                           \
 *     --authenticatorUid       <Authenticator UUID (ver 4), 32 hex chars>  \
 *     --nodeUidType            1                                           \
 *     --nodeUid                <Node UUID (ver 4), 32 hex chars>
 * \endcode
 */

#include "api.h"
#include "button.h"         // For Button_register_for_event()
#include "usart.h"          // For Usart_enableReceiver(), Usart_setEnabled(),
                            // Usart_receiverOn()
#include "app_setup.h"      // For App_Setup()
#include "provisioning.h"   // For Provisioning_start(),
                            // Provisioning_init_from_storage(),
                            // provisioning_conf_t
#include "app_scheduler.h"  // For App_Scheduler_addTask_execTime(),
                            // APP_SCHEDULER_STOP_TASK

#define DEBUG_LOG_MODULE_NAME "PROV APP"
#define DEBUG_LOG_MAX_LEVEL   LVL_INFO
#include "debug_log.h"  // For LOG_INIT(), LOG(), LOG_BUFFER(), LOG_LVL_*


/**
 * \brief   Start provisioning
 * \note    This function is scheduled with
 *          \ref App_Scheduler_addTask_execTime() to run immediately, so that
 *          \ref Provisioning_start() is not called from an interrupt context
 * \return  APP_SCHEDULER_STOP_TASK, i.e., not called again
 */
static uint32_t start_provisioning(void)
{
    LOG(LVL_INFO, "Provisioning starting");
    Provisioning_start();

    return APP_SCHEDULER_STOP_TASK;
}

/**
 * \brief   Select the joining beacon
 * \note    In this example the beacon with the strongest RSSI is selected, but
 *          the application is free to implement any algorithm
 * \param   beacons
 *          A pointer to the first received beacon or NULL
 * \return  Strongest beacon or NULL
 */
const app_lib_joining_received_beacon_t * select_joining_beacon(
    const app_lib_joining_received_beacon_t * beacons)
{
    const app_lib_joining_received_beacon_t * strongest_beacon = NULL;
    int16_t strongest_rssi = INT16_MIN;  // RSSI is int8_t, so > INT16_MIN

    while (beacons != NULL)
    {
        if (beacons->rssi > strongest_rssi)
        {
            strongest_beacon = beacons;
            strongest_rssi   = beacons->rssi;
        }
        beacons = beacons->next;
    }

    return strongest_beacon;
}

/**
 * \brief   Callback for received User provisioning data
 * \note    Provisioning data is received as a map of id:data. This function
 *          is callback for each id that are not reserved by Wirepas.
 * \param   id
 *          Id of the received item
 * \param   data
 *          Received data
 * \param   len
 *          Length of the data
 */
void user_data_cb(uint32_t id, CborType type, uint8_t * data, uint8_t len)
{
    LOG(LVL_INFO, "User Data, id:%d, type:%d, data:", id, type);
    LOG_BUFFER(LVL_DEBUG, data, len);
}

/**
 * \brief   The end provisioning callback, which is called
 *          at the end of the provisioning process
 * \param   result
 *          Result of the provisioning process
 * \return  True: Apply received network parameters and reboot,
 *          false: discard data and end provisioning process
 */
bool end_cb(provisioning_res_e res)
{
    LOG(LVL_INFO, "Provisioning ended with result: %d", res);

    return true;
}

/**
 * \brief   Button 0 callback: starts the provisioning process
 * \param   button_id
 *          Number of the pressed button, unused
 * \param   event
 *          Button event, unused
 */
void button_0_cb(uint8_t button_id, button_event_e event)
{
    (void) button_id;
    (void) event;

    // This function is run in interrupt context, so
    // schedule a separate task to start provisioning
    App_Scheduler_addTask_execTime(start_provisioning,
                                   APP_SCHEDULER_SCHEDULE_ASAP,
                                   2000);
}

/**
 * \brief   USART RX callback: starts the provisioning process
 * \param   ch
 *          Received characters
 * \param   n
 *          Number of received characters
 */
void usart_rx_cb(uint8_t * ch, size_t n)
{
    if ((n == 1) && (*ch == ' '))  // Space
    {
        button_0_cb(0, 0);
    }
}

void App_init(const app_global_functions_t * functions)
{
    (void) functions;

    LOG_INIT();
    LOG(LVL_INFO, "Starting");

    /* Set up initial node configuration */
    App_Setup();

    /* Print node configuration */
    app_addr_t                     node_addr;
    app_lib_settings_net_addr_t    net_addr;
    app_lib_settings_net_channel_t net_channel;
    app_lib_settings_role_t        node_role;
    lib_settings->getNodeAddress(&node_addr);
    lib_settings->getNetworkAddress(&net_addr);
    lib_settings->getNetworkChannel(&net_channel);
    lib_settings->getNodeRole(&node_role);
    LOG(LVL_INFO, "Node configuration:");
    LOG(LVL_INFO, "  - node addr: %d", node_addr);
    LOG(LVL_INFO, "  - net addr:  0x%06X", net_addr);
    LOG(LVL_INFO, "  - net ch:    %d", net_channel);
    LOG(LVL_INFO, "  - node role: 0x%02X", node_role);

    provisioning_conf_t prov_conf
        = { .end_cb            = end_cb,
            .user_data_cb      = user_data_cb,
            .beacon_joining_cb = select_joining_beacon };

    /* Load provisioning parameters from secure storage */
    Provisioning_init_from_storage(&prov_conf);

    /* Start provisioning on button press */
    Button_register_for_event(0, BUTTON_PRESSED, button_0_cb);

    /* Start provisioning on received USART space character */
    Usart_enableReceiver(usart_rx_cb);
    Usart_setEnabled(true);
    Usart_receiverOn();

    /* Start the stack */
    lib_state->startStack();
}
