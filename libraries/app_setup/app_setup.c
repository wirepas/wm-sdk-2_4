/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/**
 * Public and internal library headers
 */
#include "app_setup.h"
#include "app_setup_int.h"

/**
 * Stack dependencies
 */
#include "wms_settings.h"
#include "wms_storage.h"
#include "wms_data.h"

/**
 * SDK dependencies
 */
#include "node_configuration.h"
#include "app_persistent.h"

#ifdef PROVISIONING
#include "provisioning.h"
#endif

#define DEBUG_LOG_MODULE_NAME "APP_SETUP"
#define DEBUG_LOG_MAX_LEVEL LVL_INFO

#include "debug_log.h"

/**
 * @brief   Most recent supported version.
 */
#define APP_SETUP_MAX_VERSION 1

#define EMPTY_NODE_ID 0xFFFFFFFF

/**
 * @brief   Check error from result value
 *
 *          Generic error validator to save flash space.
 *
 * @param[out]  error   Pointer to boolean that will be set in case of error
 * @param[in]   res     Result value where 0 is non-error
 * @param[in]   func    Function name whose error is being checked
 */
static void check_res_error(bool * const error,
                            int8_t res,
                            const char * const func)
{
    (void) func;

    if (res != 0)
    {
        LOG(LVL_ERROR, "%s:%d", func, res);

        *error = true;
    }
}

/**
 * @brief   Validate the header
 *
 * @param[in]   header  Pointer to the header structure
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e validate_header(
    const app_setup_hdr_t * const header)
{
    if (header->format != APP_SETUP_FORMAT)
    {
        return APP_SETUP_RES_NO_DATA;
    }

    if (header->version > APP_SETUP_MAX_VERSION)
    {
        return APP_SETUP_RES_INVALID_VERSION;
    }

    if (header->length != sizeof(app_setup_t))
    {
        return APP_SETUP_RES_INVALID_DATA;
    }

    return APP_SETUP_RES_OK;
}

/**
 * @brief   Check whether node configuration is not empty
 *
 * @param[in]   node    Pointer to node configuration
 *
 * @return  true    Node configuration is set
 *          false   Node configuration is empty
 */
static bool check_node(const app_setup_node_t * const node)
{
    app_setup_node_t empty_node;
    memset(&empty_node, APP_SETUP_CLEAR_BYTE, sizeof(app_setup_node_t));

    return (memcmp(node, &empty_node, sizeof(app_setup_node_t)))
            ? true
            : false;
}

/**
 * @brief   Store node configuration to settings
 *
 * @param[in]   node    Pointer to node configuration
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_node(const app_setup_node_t * const node)
{
    bool error = false;

    check_res_error(&error,
                    lib_settings->setNodeAddress(
                        (node->id != EMPTY_NODE_ID) ? node->id
                                                    : getUniqueAddress()
                    ),
                    "setNodeAddress()");
    check_res_error(&error,
                    lib_settings->setNodeRole(node->role),
                    "setNodeRole()");
    check_res_error(&error,
                    lib_settings->setNetworkAddress(node->network_address),
                    "setNetworkAddress()");
    check_res_error(&error,
                    lib_settings->setNetworkChannel(node->network_channel),
                    "setNetworkChannel()");

    if ((node->role == APP_LIB_SETTINGS_ROLE_SINK_LL) ||
        (node->role == APP_LIB_SETTINGS_ROLE_SINK_LE))
    {
        check_res_error(&error,
                        lib_data->writeDiagnosticInterval(node->diag_interval),
                    "writeDiagnosticInterval()");
    }

    return (error) ? APP_SETUP_RES_ERROR : APP_SETUP_RES_OK;
}

/**
 * @brief   Check whether key management configuration is not empty
 *
 * @param[in]   keys    Pointer to node configuration
 *
 * @return  true    Key management configuration is set
 *          false   Key management configuration is empty
 */
static bool check_key_mgmt(const app_setup_key_mgmt_t * const keys)
{
    app_setup_key_mgmt_t empty_keys;
    memset(&empty_keys, APP_SETUP_CLEAR_BYTE, sizeof(app_setup_key_mgmt_t));
    return (memcmp(keys, &empty_keys, sizeof(app_setup_key_mgmt_t)))
            ? true
            : false;
}

/**
 * @brief   Convert key management setup to settings library configuration
 *
 * @param[out]  app_lib_settings_keys   Pointer to settings library keys
 *
 * @param[in]   app_setup_keys  Pointer to application setup keys
 */
static void get_settings_key_mgmt(
    app_lib_settings_key_management_configuration_t * const lib_settings_keys,
    const app_setup_key_mgmt_t * const app_setup_keys
)
{
    *lib_settings_keys = (app_lib_settings_key_management_configuration_t)
    {
        .network_key_pair =
        {
            .encryption_key_p     = &app_setup_keys->network_enc_key,
            .authentication_key_p = &app_setup_keys->network_auth_key,
        },
        .management_key_pair =
        {
            .encryption_key_p     = &app_setup_keys->mgmt_enc_key,
            .authentication_key_p = &app_setup_keys->mgmt_auth_key,
        },
        .flags =
        {
            .key_management = app_setup_keys->enable_key_management,
            .revoke_keys = app_setup_keys->force_key_revocation,
        },
        .network_key_pair_seq = app_setup_keys->network_key_pair_seq,
        .management_key_pair_seq = app_setup_keys->mgmt_key_pair_seq,
    };
}

/**
 * @brief   Store key management configuration to settings
 *
 * @param[in]   keys    Pointer to key management configuration
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_key_mgmt(const app_setup_key_mgmt_t * const keys)
{
    app_lib_settings_key_management_configuration_t app_lib_keys = { 0 };
    bool error = false;

    get_settings_key_mgmt(&app_lib_keys, keys);

    check_res_error(&error,
                    lib_settings->keyManagementConfiguration(&app_lib_keys),
                    "lib_settings->keyManagementConfiguration()");

    return (error) ? APP_SETUP_RES_ERROR : APP_SETUP_RES_OK;
}

#ifdef PROVISIONING

/**
 * @brief   Check whether provisioning configuration is not empty
 *
 * @param[in]   provisioning_setup  Pointer to node configuration
 *
 * @return  true    Provisioning configuration is set
 *          false   Provisioning configuration is empty
 */
static bool check_provisioning(
    const app_setup_provisioning_t * const provisioning_setup)
{
    app_setup_provisioning_t empty_provisioning_setup;
    memset(&empty_provisioning_setup,
           APP_SETUP_CLEAR_BYTE,
           sizeof(app_setup_provisioning_t));

    return (memcmp(provisioning_setup,
                   &empty_provisioning_setup,
                   sizeof(app_setup_provisioning_t)))
            ? true
            : false;
}

/**
 * @brief   Dummy implementation of provisioning end callback
 *
 *          In case application did not set provisioning callbacks
 *          when calling App_Setup(), a dummy callback will just log
 *          the event.
 *
 * @param[in]   result  Provisioning result
 *
 * @return  Always true
 */
static bool dummy_prov_end_cb(provisioning_res_e result)
{
    (void) result;

    LOG(LVL_INFO, "Provisioning end res: %d", result);

    return true;
}

/**
 * @brief   Dummy implementation of provision user data callback
 *
 *          In case application did not set provisioning callbacks
 *          when calling App_Setup(), a dummy callback will just log
 *          the event.
 *
 * @param[in]   id      Id of the received item
 * @param[in]   type    Type of data
 * @param[in]   data    Received data
 * @param[in]   len     Length of the data
 */
static void dummy_prov_user_data_cb(uint32_t id,
                                    CborType type,
                                    uint8_t * data,
                                    uint8_t len)
{
    (void) id;
    (void) type,
    (void) data;
    (void) len;

    LOG(LVL_INFO, "Prov user data, id:%d,type:%d,len:%d", id, type, len);
}

/**
 * @brief   Dummy implementation of provision joining beacon RX callback
 *
 *          In case application did not set provisioning callbacks
 *          when calling App_Setup(), a dummy callback will just log
 *          the event.
 *
 * @param[in]   beacons Pointer to list of received beacons
 *
 * @return  A pointer to selected beacon
 */
static const app_lib_joining_received_beacon_t *
    dummy_prov_joining_beacon_rx_cb(
        const app_lib_joining_received_beacon_t * const beacons)
{
    assert(beacons != NULL);

    LOG(LVL_INFO, "Prov recv joining beacon. t:%d,c:%d",
                  beacons->type,
                  beacons->channel);

    return beacons;
}

/**
 * @brief   Store provisioning configuration to settings
 *
 * @param[in]   provisioning_setup  Pointer to provisioning configuration
 * @param[in]   key_mgmt_setup  Pointer to key management configuration
 * @param[in]   conf    Pointer to provisioning callbacks from application
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_provisioning(
    const app_setup_provisioning_t * const provisioning_setup,
    const app_setup_key_mgmt_t * const key_mgmt_setup,
    const struct setup_provisioning_conf * const conf
)
{
    uint8_t secure_method_keys[APP_SETUP_KEY_SIZE_BYTES * 2] = { 0 };
    uint8_t secure_method_key_len = 0;

    app_lib_settings_key_management_configuration_t app_lib_keys = { 0 };

    if (provisioning_setup->method == PROV_METHOD_SECURED)
    {
        memcpy(&secure_method_keys,
               provisioning_setup->auth_key,
               APP_SETUP_KEY_SIZE_BYTES);
        memcpy(&secure_method_keys[APP_SETUP_KEY_SIZE_BYTES],
               provisioning_setup->enc_key,
               APP_SETUP_KEY_SIZE_BYTES);
        secure_method_key_len = APP_SETUP_KEY_SIZE_BYTES * 2;
    }
    else if (provisioning_setup->method == PROV_METHOD_KEY_MGMT)
    {
        get_settings_key_mgmt(&app_lib_keys, key_mgmt_setup);
    }

    provisioning_ret_e prov_ret = Provisioning_init(&(provisioning_conf_t)
    {
        .method = provisioning_setup->method,
        .nb_retry = provisioning_setup->num_retries,
        .timeout_s = provisioning_setup->timeout,
        .uid = &provisioning_setup->uid[0],
        .uid_len = provisioning_setup->uid_len,
        .key = &secure_method_keys[0],
        .key_len = secure_method_key_len,
        .end_cb = (conf && conf->end_cb) ? conf->end_cb : dummy_prov_end_cb,
        .user_data_cb = (conf && conf->user_data_cb)
                            ? conf->user_data_cb
                            : dummy_prov_user_data_cb,
        .beacon_joining_cb = (conf && conf->joining_beacon_rx_cb)
                                ? (provisioning_joining_beacon_cb_f)
                                   conf->joining_beacon_rx_cb
                                : dummy_prov_joining_beacon_rx_cb,
        .p_key_mgmt_config = &app_lib_keys,
    });

    LOG(LVL_INFO, "Provisioning_init(): %d", prov_ret);

    return (prov_ret != PROV_RET_OK) ? APP_SETUP_RES_ERROR : APP_SETUP_RES_OK;
}

#endif

/**
 * @brief   Set dualMCU application automatic start setting
 *
 * @param[in] start_dualmcu 1 to start, 0 to not start
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_dualmcu_start(uint8_t start_dualmcu)
{
    bool error = false;
    check_res_error(&error,
                    lib_storage->writePersistent(&start_dualmcu, 1),
                    "lib_storage->writePersistent()");

    return (error) ? APP_SETUP_RES_ERROR : APP_SETUP_RES_OK;
}

/**
 * @brief   Load setup from application persistent area
 *
 * @param[out]  setup   Pointer to setup instance where to load the data
 *
 * @return  \ref app_setup_res_e
 */
app_setup_res_e setup_load(app_setup_t * const setup)
{
    bool error = false;

    /**
     * Initialize App_Persistent library just in case.
     *
     * If it was already initialized, call does nothing
     */
    check_res_error(&error, App_Persistent_init(), "App_Persistent_init()");
    if (error)
    {
        return APP_SETUP_RES_NO_APP_PERSISTENT;
    }

    /**
     * Read format to check whether application persistent area in the
     * flash has been intialized.
     */
    uint32_t format;
    if (App_Persistent_read((uint8_t*)&format, sizeof(uint32_t))
                        == APP_PERSISTENT_RES_INVALID_CONTENT)
    {
        return APP_SETUP_RES_NO_DATA;
    }

    app_setup_hdr_t hdr;
    size_t hdr_len = sizeof(app_setup_hdr_t);
    check_res_error(&error,
                    App_Persistent_read((uint8_t*)&hdr, hdr_len),
                    "App_Persistent_read()");
    if (error)
    {
        return APP_SETUP_RES_ERROR;
    }

    app_setup_res_e res = validate_header(&hdr);
    if (res != APP_SETUP_RES_OK)
    {
        return res;
    }

    check_res_error(&error,
                    App_Persistent_read((uint8_t*)setup, sizeof(app_setup_t)),
                    "App_Persistent_read()");
    if (error)
    {
        return APP_SETUP_RES_NO_APP_PERSISTENT;
    }

    if (setup->action.no_erase_after != 1)
    {
        uint8_t erase_data[sizeof(app_setup_t)];
        memset(erase_data, APP_SETUP_CLEAR_BYTE, sizeof(app_setup_t));

        check_res_error(&error,
                        App_Persistent_write(erase_data, sizeof(app_setup_t)),
                        "App_Persistent_write()");
        if (error)
        {
            return APP_SETUP_RES_ERASE_ERROR;
        }
    }

    return APP_SETUP_RES_OK;
}

/**
 * @brief   Apply setup to stack
 *
 * @param[in]   setup   Application setup instance
 * @param[in]   conf    Application setup configuration
 *
 * @return  \ref app_setup_res_e
 */
app_setup_res_e setup_apply(const app_setup_t * const setup,
                            const app_setup_conf_t * const conf)
{
    (void) conf;

    bool error = false;

    app_setup_res_e res = validate_header(&setup->header);
    if (res != APP_SETUP_RES_OK)
    {
        return res;
    }

    if (check_node(&setup->node)
        && set_node(&setup->node) != APP_SETUP_RES_OK)
    {
        error = true;
    }

#ifdef PROVISIONING
    /**
     * If provisioning method is KEY_MGMT, keys are set at provisioning init.
     */
    if (setup->provisioning.method != PROV_METHOD_KEY_MGMT
        && check_key_mgmt(&setup->key_mgmt)
        && set_key_mgmt(&setup->key_mgmt) != APP_SETUP_RES_OK)
    {
        error = true;
    }

    if (check_provisioning(&setup->provisioning)
        && set_provisioning(&setup->provisioning,
                            &setup->key_mgmt,
                            &conf->provisioning)
            != APP_SETUP_RES_OK)
    {
        error = true;
    }
#else
    if (check_key_mgmt(&setup->key_mgmt)
        && set_key_mgmt(&setup->key_mgmt) != APP_SETUP_RES_OK)
    {
        error = true;
    }
#endif

    uint8_t start_dualmcu = setup->action.start_dualmcu;
    if (start_dualmcu != APP_SETUP_CLEAR_BYTE
        && set_dualmcu_start(start_dualmcu) != APP_SETUP_RES_OK)
    {
        error = true;
    }

    return (error) ? APP_SETUP_RES_ERROR : APP_SETUP_RES_OK;
}

app_setup_res_e App_Setup(const app_setup_conf_t * const conf)
{
    app_setup_t setup;

    app_setup_res_e res = setup_load(&setup);

    if (res != APP_SETUP_RES_OK)
    {
        return res;
    }

    return setup_apply(&setup, conf);
}
