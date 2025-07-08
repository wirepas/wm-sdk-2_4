/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "provisioning.h"
#include "provisioning_int.h"
#include "app_scheduler.h"
#include "wms_state.h"
#include "stack_state.h"

#define DEBUG_LOG_MODULE_NAME "FAIL LIB"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO
#include "debug_log.h"

/** Default on how often failsafe task will be called. */
#define FAILSAFE_TASK_PERIOD_MS 5000

/** \brief Failsafe state machine states. */
typedef enum
{
    FAILSAFE_STATE_UNINIT = 0, /**< Library is not initialized. */
    FAILSAFE_STATE_INIT = 1,
    FAILSAFE_STATE_IDLE = 2,
    FAILSAFE_STATE_NO_ROUTE = 3,
    FAILSAFE_STATE_WAIT_TIMEOUT = 4,
    FAILSAFE_STATE_START_PROVISIONING = 5,
} failsafe_state_e;

/** \brief Failsafe events. */
static struct
{
    uint8_t route_lost:1; /**< Route to sink lost. */
    uint8_t route_found:1; /**< Route to sink found. */
    uint8_t beacons_received:1; /**< Unsecured beacons received */
    uint8_t timeout:1; /**< Timeout occurred */
} m_events;

/** \brief Current state of the state maching */
static failsafe_state_e m_state;

/** \brief Failsafe configuration */
static provisioning_failsafe_conf_t m_conf;

/**
 * \brief   Reset failsafe.
 */
static void reset_failsafe(void);

/**
 * \brief  Clear all events.
 */ 
static void clear_events(void)
{
    memset(&m_events,0,sizeof(m_events));
}

/**
 * \brief   Handle beacon received callback.
 */
static void handle_beacon_received(const app_lib_state_beacon_rx_t * beacon)
{
    LOG(LVL_DEBUG, "Provisioning failsafe received beacon.");
    LOG(LVL_DEBUG, " - address: %d", beacon->address);
    LOG(LVL_DEBUG, " - rssi: %d", beacon->rssi);
    LOG(LVL_DEBUG, " - txpower: %d", beacon->txpower);
    LOG(LVL_DEBUG, " - is_sink: %d", beacon->is_sink);
    LOG(LVL_DEBUG, " - is_ll: %d", beacon->is_ll);
    LOG(LVL_DEBUG, " - cost: %d", beacon->cost);
    LOG(LVL_DEBUG, " - type: %d", beacon->type);
    LOG(LVL_DEBUG, " - is_da_support: %d", beacon->is_da_support);
    LOG(LVL_DEBUG, " - secured: %d", beacon->is_secured);

    if (beacon->type == APP_LIB_STATE_BEACON_TYPE_NB
        && beacon->is_secured == false)
    {
        m_events.beacons_received = 1;
    }
}

/**
 * \brief   Task that handles timeout.
 */
static uint32_t timeout_task(void)
{
    m_events.timeout = 1;

    return APP_SCHEDULER_STOP_TASK;
}

/**
 * \brief   Handle route change.
 */
static void handle_route_change(const app_lib_state_route_info_t * route_info)
{
    LOG(LVL_DEBUG, "Provisioning failsafe received route change.");
    LOG(LVL_DEBUG, " - state: %d", route_info->state);
    LOG(LVL_DEBUG, " - sink: %d", route_info->sink);
    LOG(LVL_DEBUG, " - next_hop: %d", route_info->next_hop);
    LOG(LVL_DEBUG, " - channel: %d", route_info->channel);
    LOG(LVL_DEBUG, " - cost: %d", route_info->cost);
    
    if (route_info->sink == 0)
    {
        m_events.route_lost = 1;
    }
    else if (route_info->state == APP_LIB_STATE_ROUTE_STATE_VALID &&
             route_info->sink != 0 && route_info->next_hop != 0 &&
             route_info->channel != 0 &&
             route_info->cost != APP_LIB_STATE_INVALID_ROUTE_COST)
    {
        m_events.route_found = 1;
    }
}

/**
 * \brief   Callback to trigger provisioning.
 */
static void handle_stack_event(app_lib_stack_event_e event, void *param)
{
    LOG(LVL_DEBUG, "%s : Stack event: %d.", __func__, event);
    switch (event)
    {
        /** Explicit order to start provisioning */
        case APP_LIB_STATE_STACK_EVENT_CON_FALLBCK:
            reset_failsafe();
            m_conf.start_cb();
            break; 
        case APP_LIB_STATE_STACK_EVENT_ROUTE_CHANGED:
            handle_route_change((const app_lib_state_route_info_t *)param);
            break;
        default:
            (void) event;
            break;
    }
}

/**
 * \brief   Transition to idle state.
 */
static void transition_to_idle(void)
{
    clear_events();
    lib_state->setOnBeaconCb(NULL);
    App_Scheduler_cancelTask(timeout_task);

    m_state = FAILSAFE_STATE_IDLE;
}

/**
 * \brief   Transition to no route state.
 */
static void transition_to_no_route(void)
{
    clear_events();
    lib_state->setOnBeaconCb(handle_beacon_received); 

    m_state = FAILSAFE_STATE_NO_ROUTE;
}

/**
 * \brief   Transition to wait timeout state.
 */
static void transition_to_wait_timeout(void)
{
    clear_events();
    m_state = FAILSAFE_STATE_WAIT_TIMEOUT;
        
    if (App_Scheduler_addTask_execTime(timeout_task,
                                       (m_conf.timeout_sec * 1000),
                                       500) != APP_SCHEDULER_RES_OK)
    {
        LOG(LVL_ERROR, "%s : Error to add task.", __func__);
        reset_failsafe();
    }
}

/**
 * \brief   Idle state.
 *
 */
static uint32_t state_idle(void)
{
    uint32_t new_delay_ms = FAILSAFE_TASK_PERIOD_MS;

    if (m_events.route_lost)
    {
        transition_to_no_route();
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }

    return new_delay_ms;
}

/**
 * \brief   No route state.
 */
static uint32_t state_no_route(void)
{
    uint32_t new_delay_ms = FAILSAFE_TASK_PERIOD_MS;

    if (m_events.route_found)
    {
        transition_to_idle();
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }
    else if (m_events.beacons_received)
    {
        transition_to_wait_timeout();
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }

    return new_delay_ms;
}

/**
 * \brief   Wait timeout state.
 */
static uint32_t state_wait_timeout(void)
{
    uint32_t new_delay_ms = FAILSAFE_TASK_PERIOD_MS;

    if (m_events.route_found)
    {
        transition_to_idle();
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }
    else if (m_events.timeout)
    {
        clear_events();
        m_state = FAILSAFE_STATE_START_PROVISIONING;
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }

    return new_delay_ms;
}

/**
 * \brief   Start provisioning
 */
static uint32_t state_start_provisioning(void)
{
    uint32_t new_delay_ms = APP_SCHEDULER_STOP_TASK;

    reset_failsafe();

    LOG(LVL_INFO, "Failsafe module starts provisioning.");

    m_conf.start_cb();

    return new_delay_ms;
}

/**
 * \brief   Provisioning failsafe state machine. Reacting to events raised in
 *          this module.
 */
static uint32_t run_state_machine(void)
{
    uint32_t new_delay_ms = FAILSAFE_TASK_PERIOD_MS;

    LOG(LVL_DEBUG, "%s : Enter state machine :", __func__);
    LOG(LVL_DEBUG, " - state : %d", m_state);
    LOG(LVL_DEBUG, " - events :%s%s%s%s",
                   m_events.route_lost ? " route_lost" : "",
                   m_events.route_found ? " route_found" : "",
                   m_events.beacons_received ? " beacons_received" : "",
                   m_events.timeout ? " timeout" : "");

    switch (m_state)
    {
        case FAILSAFE_STATE_IDLE:
        {
            new_delay_ms = state_idle();
            break;
        }

        case FAILSAFE_STATE_NO_ROUTE:
        {
            new_delay_ms = state_no_route();
            break;
        }

        case FAILSAFE_STATE_WAIT_TIMEOUT:
        {
            new_delay_ms = state_wait_timeout();
            break;
        }

        case FAILSAFE_STATE_START_PROVISIONING:
        {
            new_delay_ms = state_start_provisioning();
            break;
        }

        default:
        {
            break;
        }
    }

    return new_delay_ms;
}

static void reset_failsafe(void)
{
    LOG(LVL_DEBUG, "Reset failsafe to init state.");
    clear_events();
    lib_state->setOnBeaconCb(NULL);
    App_Scheduler_cancelTask(timeout_task);
    App_Scheduler_cancelTask(run_state_machine);
    m_state = FAILSAFE_STATE_INIT;
}

provisioning_ret_e Provisioning_Failsafe_init(
    const provisioning_failsafe_conf_t * conf)
{
    LOG(LVL_DEBUG, "%s, Configuration:",__func__);

    if (m_state != FAILSAFE_STATE_UNINIT)
    {
        LOG(LVL_ERROR,
            "Failsafe init, PROV_RET_INVALID_STATE, (state:%d)", m_state);
        return PROV_RET_INVALID_STATE;
    }

    if (conf == NULL || conf->start_cb == NULL || conf->timeout_sec == 0)
    {
        LOG(LVL_ERROR, "Failsafe init, PROV_RET_INVALID_PARAM");
        return PROV_RET_INVALID_PARAM;
    }

    LOG(LVL_DEBUG, " - Timeout (sec): %d", conf->timeout_sec);

    memcpy(&m_conf,conf,sizeof(provisioning_failsafe_conf_t));

    reset_failsafe();

    return PROV_RET_OK;
}

provisioning_ret_e Provisioning_Failsafe_start(void)
{
    if (m_state != FAILSAFE_STATE_INIT)
    {
        LOG(LVL_ERROR, "%s:PROV_RET_INVALID_STATE:%d",__func__, m_state);
        return PROV_RET_INVALID_STATE;
    }

    lib_state->setOnStackEventCb(handle_stack_event);

    app_lib_state_route_info_t route_info;
    lib_state->getRouteInfo(&route_info);
    
    if (route_info.sink)
    {
        transition_to_idle();
    }
    else
    {
        m_state = FAILSAFE_STATE_NO_ROUTE;
    }

    if (App_Scheduler_addTask_execTime(run_state_machine,
                                       APP_SCHEDULER_SCHEDULE_ASAP,
                                       500) != APP_SCHEDULER_RES_OK)
    {
        LOG(LVL_ERROR, "%s: Error adding task.", __func__);
        reset_failsafe();
        return PROV_RET_INTERNAL_ERROR;
    }

    return PROV_RET_OK;
}

provisioning_ret_e Provisioning_Failsafe_stop(void)
{
    if (m_state == FAILSAFE_STATE_UNINIT || m_state == FAILSAFE_STATE_INIT)
    {
        return PROV_RET_INVALID_STATE;
    }

    reset_failsafe();

    LOG(LVL_INFO, "Provisioning failsafe stopped.");

    return PROV_RET_OK;
}
