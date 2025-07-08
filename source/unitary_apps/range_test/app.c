/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * \file    app.c
 * \brief   This application scans network beacons, stores the best neighbors in
 * radio range and sends them regularly to a companion app using mini-beacons.
 * The measurements are also sent to the sink.
 *
 */

#include <stdlib.h>

#include "api.h"
#include "node_configuration.h"
#include "app_scheduler.h"
#include "shared_appconfig.h"
#include "shared_data.h"

#include "measurements.h"
#include "event.h"

#include "led.h"

#define DEBUG_LOG_MODULE_NAME "SCANNER"
#define DEBUG_LOG_MAX_LEVEL   LVL_DEBUG
#include "debug_log.h"

/** Period to send data */
#define DEFAULT_PERIOD_S         15
#define DO_SCAN_INITIAL_DELAY_MS (5 * 1000)
#define DO_SCAN_EXEC_TIME_US     (100)

/** 2 minutes delay */
#define SET_BACKGROUND_MODE_DELAY_S  120
#define SET_BACKGROUND_MODE_DELAY_MS (SET_BACKGROUND_MODE_DELAY_S * 1000)

#define SET_BACKGROUND_MODE_EXEC_TIME_US 100

#define SCAN_DURATION          (1.5 * 1000)
#define STOP_SCAN_EXEC_TIME_US (100)

#define APP_CONFIG_SCAN_MODE_PERIOD 0xA6

typedef enum
{
    SCAN_MODE_BACKGROUND = 0,  // Scan made by the stack (opportunistic)
    SCAN_MODE_ACTIVE     = 1   // Scan performed when the route to sink is lost
} scan_mode_t;

typedef enum
{
    FORCE_ACTIVE_SCAN_MODE_DISABLED = 0,  // Scan mode is managed by the app
    FORCE_ACTIVE_SCAN_MODE_ENABLED  = 1   // Scan mode is forced to active mode
} force_active_scan_mode_t;

/** Scan tasks declarations  */
static uint32_t do_scan(void);
static uint32_t stopScanNbors(void);
static uint32_t set_background_mode_task(void);

static app_addr_t                 m_addr       = 0;
static app_lib_state_route_info_t m_route_info = { 0 };

static scan_mode_t m_scan_mode = SCAN_MODE_BACKGROUND;

static force_active_scan_mode_t m_force_active_mode
    = FORCE_ACTIVE_SCAN_MODE_DISABLED;

static uint8_t scan_period_s;

/**
 * \brief Function responsible to start scanning process
 *
 * \param mode Current scan mode
 */
static void scan_mode_management(scan_mode_t mode)
{
    if (m_force_active_mode || mode == SCAN_MODE_ACTIVE)
    {
        LOG(LVL_INFO,
            "Scan mode set to: SCAN_MODE_ACTIVE (Forced: %s)",
            m_force_active_mode ? "True" : "False");

        m_scan_mode = SCAN_MODE_ACTIVE;
        /** Do not switch to Background Mode if it was previously
            requested */
        App_Scheduler_cancelTask(set_background_mode_task);
        /** In active mode, application schedules scan on its own */
        App_Scheduler_addTask_execTime(do_scan,
                                       DO_SCAN_INITIAL_DELAY_MS,
                                       DO_SCAN_EXEC_TIME_US);
    }
    else
    {
        LOG(LVL_INFO, "Scan mode set to: SCAN_MODE_BACKGROUND");
        m_scan_mode = SCAN_MODE_BACKGROUND;
        /** Cancel Scheduled Active Mode tasks */
        App_Scheduler_cancelTask(do_scan);
        App_Scheduler_cancelTask(stopScanNbors);
    }
}

/**
 * \brief Set the background mode
 * \return uint32_t Rescheduling delay in ms
 */
static uint32_t set_background_mode_task(void)
{
    scan_mode_management(SCAN_MODE_BACKGROUND);
    return APP_SCHEDULER_STOP_TASK;
}

/**
 * \brief Append own installation quality information to the measurements table
 *
 */
static void update_and_insert_own_information(void)
{
    /** We insert only relevent informations, used by the Measurements module
     (i.e. Node Address, RSSI, Cost) */
    lib_state->getRouteInfo(&m_route_info);

    /** RSSI set to 0 as device does not have RSSI measurement towards itself */
    const app_lib_state_beacon_rx_t own_info
        = { .address = m_addr, .cost = m_route_info.cost, .rssi = 0 };

    Event_beacon_reception(&own_info);
}

/**
 * \brief Function to initialize scan start, no matter of the scan origin
 *
 */
static void scan_start_init(void)
{
    Measurements_table_reset();
    update_and_insert_own_information();

    lib_state->setOnBeaconCb(Event_beacon_reception);
    lib_state->startScanNbors();
}

/**
 * \brief  Periodic callback, called when the app wants to scan the neighbors
 * \return uint32_t Rescheduling delay in ms
 */
static uint32_t do_scan(void)
{
    LOG(LVL_INFO, "Neighbor Scan Started in Active mode");
    scan_start_init();

    App_Scheduler_addTask_execTime(stopScanNbors,
                                   SCAN_DURATION,
                                   STOP_SCAN_EXEC_TIME_US);

    return APP_SCHEDULER_STOP_TASK;
}

/**
 * \brief Function to tear down the scan start state set, no matter of the scan
 * origin
 *
 */
static void scan_stopped_tear_down(void)
{
    lib_state->setOnBeaconCb(NULL);

    Measurements_order_beacons_by_rssi();

    /** SEND */
    Event_network_scan_end();
}

/**
 * \brief  Periodic callback, once the app requested scan is ended
 * \return uint32_t Rescheduling delay in ms
 */
static uint32_t stopScanNbors(void)
{
    LOG(LVL_INFO, "Scan Ended in Active mode");
    lib_state->stopScanNbors();

    scan_stopped_tear_down();

    /** RESCHEDULE */
    if (m_scan_mode == SCAN_MODE_ACTIVE)
    {
        App_Scheduler_addTask_execTime(do_scan,
                                       scan_period_s * 1000,
                                       DO_SCAN_EXEC_TIME_US);
    }

    return APP_SCHEDULER_STOP_TASK;
}

/**
 * \brief   The app config type reception callback.
 *
 * This is the callback called when a new app config is received and
 * a matching type is present in the content.
 *
 * \param   type
 *          The type that match the filtering
 * \param   length
 *          The length of this TLV entry.
 * \param   value_p
 *          Pointer to the value of the TLV
 *
 * \note Details on the AppConfig format here:
 * /libraries/shared_appconfig/shared_appconfig.md
 * \note Appconfig Value 0xF67E01A50101 to enable the active scan mode forcing
 * \note Appconfig Value 0xF67E01A50100 to disable the active scan mode forcing
 */
void app_config_received_cb(uint16_t type, uint8_t length, uint8_t * value_p)
{
    /* If the type is not present, cb will have length set to 0 and value_p set
     * to NULL */
    if (value_p == NULL || length == 0)
    {
        return;
    }

    /* Checking the received value has the correct type & length*/
    if (type != APP_CONFIG_SCAN_MODE_PERIOD || length != sizeof(uint8_t))
    {
        return;
    }

    LOG(LVL_INFO, "Received app_config");

    LOG(LVL_WARNING, "Set new scan period to %u sec", *value_p);
    scan_period_s = *value_p;
}

/**
 * \brief   Function type stack events callback
 * \param   event
 *          Which event generated this call
 * \param   param_p
 *          Parameter pointer associated to the event. This pointer
 *          must be casted depending on the event. Its type is
 *          listed in \ref app_lib_stack_event_e
 * \note    Most of the time this callback is generated in critical
 *          section of the stack code so execution time must be short
 * \note    List of event may evolve in future. To write forward compatible code
 *          callback must discard unknown event
 */
static void on_stack_event_cb(app_lib_stack_event_e event, void * param)
{
    app_lib_state_route_info_t * new_route;

    switch (event)
    {
        case APP_LIB_STATE_STACK_EVENT_SCAN_STARTED:
            /* Take into account only if in SCAN_MODE_BACKGROUND, as it's
             already processed by do_scan in SCAN_MODE_ACTIVE */
            if (m_scan_mode == SCAN_MODE_BACKGROUND)
            {
                LOG(LVL_INFO, "Neighbor Scan Started in Background mode");
                scan_start_init();
            }
            break;
        case APP_LIB_STATE_STACK_EVENT_SCAN_STOPPED:
            /* Take into account only if in SCAN_MODE_BACKGROUND, as it's
                 already processed by stopScanNbors in SCAN_MODE_ACTIVE */
            if (m_scan_mode == SCAN_MODE_BACKGROUND)
            {
                LOG(LVL_INFO, "Neighbor Scan Ended in Background mode");
                scan_stopped_tear_down();
            }
            break;
        case APP_LIB_STATE_STACK_EVENT_ROUTE_CHANGED:
            new_route = (app_lib_state_route_info_t *) param;
            if (new_route->state != APP_LIB_STATE_ROUTE_STATE_VALID)
            {
                LOG(LVL_INFO, "Route to sink lost!");
                /** Route to sink lost, perform active scan */
                scan_mode_management(SCAN_MODE_ACTIVE);
            }
            else
            {
                LOG(LVL_INFO, "Route to sink found!");
            }
            break;
        default:
            /** Nothing to do. New event may be generated in later release*/
            (void) event;
    }
}

/**
 * \brief   Initialization callback for application
 *
 * This function is called after hardware has been initialized but the
 * stack is not yet running.
 *
 */
void App_init(const app_global_functions_t * functions)
{
    LOG_INIT();
    LOG(LVL_INFO, "Starting network beacon scanner");

    /* Filter id registered */
    static uint16_t m_filter_id;

    app_lib_settings_net_channel_t chan     = 0;
    app_lib_settings_net_addr_t    nwk_addr = 0;

    scan_period_s = DEFAULT_PERIOD_S;

    /** Basic configuration of the node with a unique node address */
    if (configureNodeFromBuildParameters() != APP_RES_OK)
    {
        /** Could not configure the node
        It should not happen except if one of the config value is invalid */
        return;
    }

    lib_settings->getNodeAddress(&m_addr);
    lib_settings->getNetworkAddress(&nwk_addr);
    lib_settings->getNetworkChannel(&chan);

    lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_HEADNODE_LL);


    LOG(LVL_INFO,
        "NodeAddr= 0x%08x | NetAddr= 0x%08x | NetChan= %u",
        m_addr,
        nwk_addr,
        chan);

    Measurements_init();

    static shared_app_config_filter_t filter
        = { .type           = APP_CONFIG_SCAN_MODE_PERIOD,
            .cb             = app_config_received_cb,
            .call_cb_always = true };

    Shared_Appconfig_addFilter(&filter, &m_filter_id);

    /** Default scan mode is Active mode while route to sink isn't acquired */
    scan_mode_management(SCAN_MODE_ACTIVE);

    lib_state->setOnStackEventCb(on_stack_event_cb);

    /** Start the stack */
    lib_state->startStack();
}
