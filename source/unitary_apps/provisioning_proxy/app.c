/* Copyright 2019 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/*
 * \file    app.c
 * \brief   This file is an application example to use the provisioning proxy
 *          library.
 */

#include "api.h"
#include "button.h"
#include "node_configuration.h"
#include "provisioning.h"
#include "shared_data.h"

#define DEBUG_LOG_MODULE_NAME "PROXY APP"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO
#include "debug_log.h"

/** Transmission power to send joining beacons, in dBm */
#define JOINING_TX_POWER        8

/** Define this for new node to be low-latency */
//#define LOW_LATENCY_NODE 1

/** Define this to true to enable local provisioning. */
#define ENABLE_LOCAL_PROVISIONING true

/** Network authentication key. */
static const uint8_t authentication_key[] =  {NET_AUTHEN_KEY};

/** Network encryption key. */
static const uint8_t encryption_key[] =  {NET_CIPHER_KEY};

/** Sending joining beacons is enabled or not. */
static bool beacon_tx_enabled = false;

/**
 * \brief   Button 0 callback: start or stop sending joining beacons
 * \param   button_id
 *          Number of the pressed button, unused
 * \param   event
 *          Button event, unused
 */
static void button_0_cb(uint8_t button_id, button_event_e event)
{
    (void)button_id;
    (void)event;

    /* Turn joining beacon transmission on or off. */
    if (beacon_tx_enabled)
    {
        Provisioning_Proxy_stop();
        beacon_tx_enabled = false;
    }
    else
    {
        Provisioning_Proxy_start();
        beacon_tx_enabled = true;
    }
}

/**
 * \brief   Always accept the new node. Send the same network parameters than
 *          what is configured for the proxy.
 * \param   p_uid
 *          A pointer to the node UID.
 * \param   uid_len
 *          The size in bytes of the UID
 * \param   method
 *          0 for non-Extended UID methods
 * \param   p_param
 *          If returning true, the callback must fill this structure with the
 *          network parameters that will be sent to the new node.
 * \return  true: Send provisioning data to this node; false: discard the node
 *          request and reply with a NACK.
 */
bool start_cb(const uint8_t * p_uid,
              uint8_t uid_len,
              provisioning_method_e method,
              provisioning_proxy_net_param_t * p_param)
{
    (void) p_uid;
    (void) uid_len;
    provisioning_uid_t * prov_uid = (provisioning_uid_t *)p_uid;
    /* Log provisioning request details */
    LOG(LVL_INFO, "Provisioning request - Method: %d", method);

    if (PROV_METHOD_IS_EXTENDED_UID(method) )
    {
        if (prov_uid!= NULL && uid_len > 0)
        {
        LOG(LVL_INFO, "Extended UID provisioning:");
        LOG(LVL_INFO, "  Authenticator UID Type: %d", prov_uid->authenticator_uid_type);
        LOG_BUFFER(LVL_INFO, prov_uid->authenticator_uid, PROV_AUTH_UID_SIZE_BYTES);
        LOG(LVL_INFO, "  Node UID Type: %d", prov_uid->node_uid_type);
        LOG_BUFFER(LVL_INFO, prov_uid->node_uid, PROV_UUID_SIZE_BYTES);
        LOG_BUFFER(LVL_INFO, p_uid, uid_len);
        }
    }
    else if (p_uid != NULL && uid_len > 0)
    {
        LOG(LVL_INFO, "Standard provisioning - UID:");
        if (prov_uid != NULL && uid_len > 0)
        {
            LOG_BUFFER(LVL_INFO, prov_uid->uid, uid_len);
        }
    }

    /* The proxy will send the same network parameters to the new node than
     * the ones it is using.
     */
    p_param->net_addr = NETWORK_ADDRESS;
    p_param->net_chan = NETWORK_CHANNEL;
    memcpy(p_param->auth_key,
           authentication_key,
           APP_LIB_SETTINGS_AES_KEY_NUM_BYTES);
    memcpy(p_param->enc_key, encryption_key, APP_LIB_SETTINGS_AES_KEY_NUM_BYTES);

    LOG(LVL_INFO, "Accepting node - sending network config");
    return true;
}


/**
 * \brief   Initialization callback for application
 *
 * This function is called after hardware has been initialized but the
 * stack is not yet running.
 */
void App_init(const app_global_functions_t * functions)
{
    (void) functions;

    /* Example of factory key used for secured method if local provisioning is
       enabled.
       In production applications, store the key in secure storage instead of
       hardcoding here. See wms_secure_storage.h. Hardcoding results in the
       data being stored as plaintext on the flash.
    */
    static const uint8_t factory_key[] = {
                                0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
                                0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
                                0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
                                0xd1, 0xd2, 0xd3, 0xd4, 0xd5
                                };
    static const uint32_t id = 0x12345678;
    provisioning_proxy_conf_t conf =
    {
        .tx_power = JOINING_TX_POWER,
        .payload = (uint8_t *)&id,
        .num_bytes = sizeof(id),
        .is_local_sec_allowed = ENABLE_LOCAL_PROVISIONING,
        .is_local_unsec_allowed = ENABLE_LOCAL_PROVISIONING,
        .key = factory_key,
        .key_len = 32,
        .start_cb = start_cb
    };

    LOG_INIT();
    LOG(LVL_INFO, "Starting");

    /* Basic configuration of the node with a unique node address. */
    if (configureNodeFromBuildParameters() != APP_RES_OK)
    {
        /* Could not configure the node. It should not happen except if one
         * of the config value is invalid.
         */
        return;
    }

    /* Proxy must be configured as Headnode. */
    /* Configure new node. */
#ifdef LOW_LATENCY_NODE
    lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_HEADNODE_LL);
#else
    lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_HEADNODE_LE);
#endif

    Provisioning_Proxy_init(&conf);

    Button_register_for_event(0, BUTTON_PRESSED, button_0_cb);

    /* Start the stack. */
    lib_state->startStack();
}
