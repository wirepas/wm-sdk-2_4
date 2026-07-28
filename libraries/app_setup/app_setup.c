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
#include "wms_state.h"
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
#define APP_SETUP_MAX_VERSION 2

/**
 * @brief   Minimum supported version
 */
#define APP_SETUP_MIN_VERSION 2

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

    if (header->version > APP_SETUP_MAX_VERSION ||
        header->version < APP_SETUP_MIN_VERSION)
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
 * @brief   Store node configuration to settings
 *
 * @note    We don't have check for empty node, because in case of node not
 *          set, we will set node address to unique device id.
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
 * @brief   Check whether network configuration is not empty
 *
 * @param[in]   keys    Pointer to node configuration
 *
 * @return  true    Network configuration is set
 *          false   Network configuration is empty
 */
static bool check_network(const app_setup_network_t * const network)
{
    app_setup_network_t empty_network;
    memset(&empty_network, APP_SETUP_CLEAR_BYTE, sizeof(app_setup_network_t));
    return (memcmp(network, &empty_network, sizeof(app_setup_network_t)))
            ? true
            : false;
}

/**
 * \brief   Check if a key is valid (not all 0x00 or all 0xFF)
 *          This function iterates through the array with indexes instead of
 *          memcmp/memset to save the CPU stack.
 * \param   key_p Pointer to the key array to validate
 * \return  Pointer to key array if valid, NULL otherwise
 */
static const uint8_t (*validate_key(const uint8_t (*key_p)[APP_SETUP_KEY_SIZE_BYTES])
                             )[APP_SETUP_KEY_SIZE_BYTES]
{
    bool has_non_zero = false;
    bool has_non_ff = false;

    if (key_p == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < APP_SETUP_KEY_SIZE_BYTES; i++) {
        if ((*key_p)[i] != 0x00)
        {
            has_non_zero = true;
        }
        if ((*key_p)[i] != 0xFF)
        {
            has_non_ff = true;
        }
        if (has_non_zero && has_non_ff)
        {
            break;
        }
    }

    if (has_non_zero && has_non_ff)
    {
        return key_p;
    }
    return NULL;
}

/**
 * @brief   Convert key management setup to settings library configuration
 *
 * @param[out]  app_lib_settings_keys   Pointer to settings library keys
 *
 * @param[in]   app_setup_keys  Pointer to application setup keys
 *
 * @return  True if key management configuration found, false otherwise
 */
static void get_settings_key_mgmt(
    app_lib_settings_key_management_configuration_t * const lib_settings_keys,
    const app_setup_network_t * const app_setup_keys
)
{
    *lib_settings_keys = (app_lib_settings_key_management_configuration_t)
    {
        .network_key_pair =
        {
            .encryption_key_p     = validate_key(&app_setup_keys->network_enc_key),
            .authentication_key_p = validate_key(&app_setup_keys->network_auth_key),
        },
        .management_key_pair =
        {
            .encryption_key_p     = validate_key(&app_setup_keys->mgmt_enc_key),
            .authentication_key_p = validate_key(&app_setup_keys->mgmt_auth_key),
        },
        {
            .apply_flags = 1,                           /* Flags must be set */
            .app_key_management_supported = 1,          /* Always enable support */
            // Never set configured flag, provisioning lib will do it later
            .app_key_management_configured = 0,
            .revoke_keys = 0,
            .reserved = 0
        },
        .network_key_pair_seq = app_setup_keys->network_key_pair_seq,
        .management_key_pair_seq = app_setup_keys->mgmt_key_pair_seq,
    };
}

/**
 * @brief   Store network configuration to settings
 *
 * @param[in]   keys    Pointer to key management configuration
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_network(const app_setup_network_t * const network)
{
    app_lib_settings_key_management_configuration_t app_lib_keys = { 0 };
    bool error = false;

    check_res_error(&error,
                    lib_settings->setNetworkAddress(network->network_address),
                    "setNetworkAddress()");

    check_res_error(&error,
                    lib_settings->setNetworkChannel(network->network_channel),
                    "setNetworkChannel()");

    get_settings_key_mgmt(&app_lib_keys, network);

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
 * @brief   Store provisioning configuration to settings
 *
 * @param[in]   provisioning_setup  Pointer to provisioning configuration
 * @param[in]   node_setup  Pointer to node configuration
 * @param[in]   conf    Pointer to provisioning callbacks from application
 *
 * @return  \ref app_setup_res_e
 */
static app_setup_res_e set_provisioning(
    const app_setup_provisioning_t * const provisioning_setup)
{
    if (provisioning_setup->prov_uid_len > APP_SETUP_PROV_UID_MAX_SIZE_BYTES)
    {
        return APP_SETUP_RES_INVALID_DATA;
    }

    provisioning_conf_t prov_conf =
    {
        .settings.method = provisioning_setup->method,
        .settings.nb_retry = provisioning_setup->num_retries,
        .settings.timeout_s = provisioning_setup->timeout,
        .settings.prov_uid_len = provisioning_setup->prov_uid_len,
    };
    /** Copy UID and keys */
    memcpy(prov_conf.settings.prov_uid.uid, provisioning_setup->prov_uid,
                    provisioning_setup->prov_uid_len);

    memcpy(&prov_conf.settings.prov_keys.enc_key,
                    &provisioning_setup->enc_key, APP_SETUP_KEY_SIZE_BYTES);
    memcpy(&prov_conf.settings.prov_keys.auth_key,
                    &provisioning_setup->auth_key, APP_SETUP_KEY_SIZE_BYTES);

    LOG(LVL_DEBUG, "Prov:M:%d,R:%d,T:%d", prov_conf.settings.method,
                                          prov_conf.settings.nb_retry,
                                          prov_conf.settings.timeout_s);

    LOG(LVL_DEBUG, "Prov UID:");
    LOG_BUFFER(LVL_DEBUG, prov_conf.settings.prov_uid.uid, prov_conf.settings.prov_uid_len);
    LOG(LVL_DEBUG, "Auth key:");
    LOG_BUFFER(LVL_DEBUG, prov_conf.settings.prov_keys.auth_key, PROV_KEY_SIZE_BYTES);
    LOG(LVL_DEBUG, "Enc key:");
    LOG_BUFFER(LVL_DEBUG, prov_conf.settings.prov_keys.enc_key, PROV_KEY_SIZE_BYTES);

    /** Store provisioning setting to secure storage */
    if (Provisioning_settings_write(&prov_conf.settings) != PROV_RET_OK)
    {
        return APP_SETUP_RES_ERROR;
    }

    return  APP_SETUP_RES_OK;
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
static app_setup_res_e setup_load(app_setup_t * const setup)
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

    if (setup->action.preserve_data != 1)
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
 * @return  true if any values written, false otherwise
 */
static bool setup_apply(const app_setup_t * const setup)
{
    bool written = false;

    if (set_node(&setup->node) == APP_SETUP_RES_OK)
    {
        written = true;
    }

    if (check_network(&setup->network)
        && set_network(&setup->network) == APP_SETUP_RES_OK)
    {
        written = true;
    }

#ifdef PROVISIONING
    if (check_provisioning(&setup->provisioning)
        && set_provisioning(&setup->provisioning)
            == APP_SETUP_RES_OK)
    {
        written = true;
    }
#endif

    uint8_t start_dualmcu = setup->action.start_dualmcu;
    if (start_dualmcu != APP_SETUP_CLEAR_BYTE
        && set_dualmcu_start(start_dualmcu) == APP_SETUP_RES_OK)
    {
        written = true;
    }

    return written;
}

app_setup_res_e App_Setup(void)
{
    app_setup_t setup;

    app_setup_res_e res = setup_load(&setup);

    if (res != APP_SETUP_RES_OK)
    {
        return res;
    }

    res = validate_header(&setup.header);

    if (res != APP_SETUP_RES_OK)
    {
        return res;
    }

    if (setup_apply(&setup))
    {
        LOG(LVL_INFO, "Success: Reboot");

        lib_state->stopStack();

        return APP_SETUP_RES_OK;
    }
    else
    {
        LOG(LVL_ERROR, "Failed");
    }

    return APP_SETUP_RES_INVALID_DATA;
}
