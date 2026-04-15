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
#include "wms_settings.h"
#include "stack_state.h"

#define DEBUG_LOG_MODULE_NAME "FAIL LIB"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO
#include "debug_log.h"

/** Default on how often failsafe task will be called. */
#define FAILSAFE_TASK_PERIOD_MS 5000

/** How many times restart is tried without reboot */
#define PROVISION_MAX_RESTARTS 20

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

/** \brief Provisioning restart counter */
static uint8_t m_prov_restart_counter;

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
    LOG(LVL_DEBUG,
        "s:%d,e:%02xh,b:a:%d,r:%d,txp:%d,c:%d,t:%d,f:%02xh",
        m_state,
        m_events,
        beacon->address,
        beacon->rssi,
        beacon->txpower,
        beacon->cost,
        beacon->type,
        beacon->is_sink       << 3 |
        beacon->is_ll         << 2 |
        beacon->is_da_support << 1 |
        beacon->is_secured    << 0);

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
    LOG(LVL_DEBUG, "s:%d,e:%02xh,ri:st:%d,s:%d,nh:%d,ch:%d,c:%d",
                    m_state,
                    m_events,
                    route_info->state,
                    route_info->sink,
                    route_info->next_hop,
                    route_info->channel,
                    route_info->cost);

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
    LOG(LVL_DEBUG, "s:%d,e:%02xh,stev:%d.", m_state, m_events, event);

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

    app_scheduler_res_e res = App_Scheduler_addTask_execTime(
                                  timeout_task,
                                  (m_conf.timeout_sec * 1000),
                                  500);

    if (res != APP_SCHEDULER_RES_OK)
    {
        LOG(LVL_ERROR, "s:%d,e:%02xh,tt:%p,r:%d",
                        m_state, m_events, timeout_task, res);

        reset_failsafe();
    }
}

/**
 * \brief   Transition to start state
 */
static void transition_to_start(void)
{
    clear_events();
    m_state = FAILSAFE_STATE_START_PROVISIONING;
}

static bool security_keys_valid(void)
{
    /** Just a key buffer to be able to check the return values */
    uint8_t key[APP_LIB_SETTINGS_AES_KEY_NUM_BYTES];

    app_res_e res = lib_settings->getEncryptionKey(key);
    if (res != APP_RES_OK)
    {
        LOG(LVL_ERROR, "ek:%d", res);
        return false;
    }

    res = lib_settings->getAuthenticationKey(key);
    if (res != APP_RES_OK)
    {
        LOG(LVL_ERROR, "ak:%d", res);
        return false;
    }

    return true;    
}

/**
 * \brief   Init state.
 */
static uint32_t state_init(void)
{
    if (security_keys_valid() == false)
    {
        LOG(LVL_INFO, "vk:0");
        transition_to_start();
    }
    else
    {
        transition_to_no_route();
    }

    return APP_SCHEDULER_SCHEDULE_ASAP;
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
        transition_to_start();
        new_delay_ms = APP_SCHEDULER_SCHEDULE_ASAP;
    }

    return new_delay_ms;
}

/**
 * \brief   Start provisioning
 */
static uint32_t state_start_provisioning(void)
{
    LOG(LVL_INFO, "s:%d,e:%02xh", m_state, m_events);

    uint32_t new_delay_ms = APP_SCHEDULER_STOP_TASK;

    reset_failsafe();

    m_conf.start_cb();

    return new_delay_ms;
}

/**
 * \brief   Provisioning failsafe state machine. Reacting to events raised in
 *          this module.
 */
static uint32_t run_state_machine(void)
{
    LOG(LVL_DEBUG, "sm:%d,e:%02xh", m_state, m_events);

    uint32_t new_delay_ms = FAILSAFE_TASK_PERIOD_MS;

    switch (m_state)
    {
        case FAILSAFE_STATE_INIT:
        {
            new_delay_ms = state_init();
            break;
        }

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

static bool add_stack_event_cbs(void)
{
    app_res_e res = Stack_State_addEventCb(
        handle_stack_event,
        1<<APP_LIB_STATE_STACK_EVENT_ROUTE_CHANGED |
        1<<APP_LIB_STATE_STACK_EVENT_CON_FALLBCK);

    if (res != APP_RES_OK)
    {
        LOG(LVL_ERROR, "s:%d,e:%02xh,r:%d", m_state, m_events, res);

        return false;
    }

    return true;
}

static void remove_stack_event_cbs(void)
{
    app_res_e res = Stack_State_removeEventCb(handle_stack_event);
    
    if (res != APP_RES_OK)
    {
        LOG(LVL_ERROR, "s:%d,e:%02xh,r:%d", m_state, m_events, res);
    }
}

static void reset_failsafe(void)
{
    LOG(LVL_DEBUG, "s:%d,e:%02xh,rst", m_state, m_events);
    clear_events();
    lib_state->setOnBeaconCb(NULL);
    remove_stack_event_cbs();
    App_Scheduler_cancelTask(timeout_task);
    App_Scheduler_cancelTask(run_state_machine);
    m_state = FAILSAFE_STATE_INIT;
}

provisioning_ret_e Provisioning_Failsafe_init(
    const provisioning_failsafe_conf_t * conf)
{
    if (conf == NULL || conf->start_cb == NULL || conf->timeout_sec == 0)
    {
        return PROV_RET_INVALID_PARAM;
    }

    LOG(LVL_DEBUG, "to:%d", conf->timeout_sec);

    if (m_state != FAILSAFE_STATE_UNINIT)
    {
        LOG(LVL_ERROR,"s:%d,exp:%d", m_state, FAILSAFE_STATE_UNINIT);

        return PROV_RET_INVALID_STATE;
    }

    memcpy(&m_conf, conf, sizeof(provisioning_failsafe_conf_t));

    reset_failsafe();

    return PROV_RET_OK;
}

provisioning_ret_e Provisioning_Failsafe_start(void)
{
    if (m_state != FAILSAFE_STATE_INIT)
    {
        LOG(LVL_ERROR, "s:%d,exp:%d,e:%02xh",
                       m_state, FAILSAFE_STATE_INIT, m_events);

        return PROV_RET_INVALID_STATE;
    }

    if (add_stack_event_cbs() == false)
    {
        return PROV_RET_INTERNAL_ERROR;
    }

    app_scheduler_res_e res = App_Scheduler_addTask_execTime(
                                  run_state_machine,
                                  APP_SCHEDULER_SCHEDULE_ASAP,
                                  500);

    if (res != APP_SCHEDULER_RES_OK)
    {
        LOG(LVL_ERROR, "s:%d,e:%02xh,sm:%p,r:%d",
                       m_state, m_events, run_state_machine, res);

        reset_failsafe();

        return PROV_RET_INTERNAL_ERROR;
    }

    return PROV_RET_OK;
}

provisioning_ret_e Provisioning_Failsafe_stop(void)
{
    LOG(LVL_INFO, "stop");

    if (m_state == FAILSAFE_STATE_UNINIT || m_state == FAILSAFE_STATE_INIT)
    {
        LOG(LVL_ERROR, "s:%d,e:%02xh,stop", m_state, m_events);

        return PROV_RET_INVALID_STATE;
    }

    reset_failsafe();

    return PROV_RET_OK;
}

bool Provisioning_Failsafe_callback_end(provisioning_res_e result)
{
    if (PROV_RES_SUCCESS == result)
    {
        LOG(LVL_DEBUG, "s:%d,e:%02xh,end:%d", m_state, m_events, result);

        m_prov_restart_counter = 0;

        // Apply parameters and reboot
        return true;
    }

    // Provisioning failed, restart
    LOG(LVL_ERROR, "s:%d,e:%02xh,end:%d", m_state, m_events, result);

    // If we have restarted enough times alread, something is seriously wrong,
    // reboot.
    if (m_prov_restart_counter < PROVISION_MAX_RESTARTS)
    { 
        m_conf.start_cb();
    }
    else
    {
        lib_state->stopStack(); // Does not return
    }

    m_prov_restart_counter++;

    // Discard the result
    return false; 
}

const app_lib_joining_received_beacon_t *
Provisioning_Failsafe_callback_beacons(
    const app_lib_joining_received_beacon_t * beacons)
{
    if (NULL == beacons)
    {
        LOG(LVL_ERROR, "bcns:0");

        return beacons;
    }

    const app_lib_joining_received_beacon_t * selected_beacon = beacons;
    const app_lib_joining_received_beacon_t * current_beacon = beacons;

    while ((current_beacon = current_beacon->next) != NULL)
    {
        if (current_beacon->rssi > selected_beacon->rssi)
        {
            selected_beacon = current_beacon;
        }
    }
    
    return selected_beacon;
}
