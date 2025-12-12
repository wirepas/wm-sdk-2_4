/* Copyright 2019 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef _PROVISIONING_H_
#define _PROVISIONING_H_

#include <stdint.h>
#include "api.h"
#include "wms_joining.h"
#include "wms_settings.h"
#include "cbor.h"

/**
 * UUID size in bytes as per RFC 4122 Section 4.1
 * "The UUID format is 16 octets"
 */
#define PROV_UUID_SIZE_BYTES 16

/**
 * @brief   All security keys are 16 bytes long
 */
#define PROV_KEY_SIZE_BYTES 16

/**
 * @brief   Maximum size of provisioning UID
 */
#define PROV_UID_MAX_SIZE_BYTES 79

/**
 * @brief   Authenticator UID size
 */
#define PROV_AUTH_UID_SIZE_BYTES 16

/** \brief Return codes of provisioning functions */
typedef enum
{
    PROV_RET_OK = 0, /**< Operation is a success. */
    PROV_RET_INVALID_STATE = 1, /**< Provisioning not in a valid state. */
    PROV_RET_INVALID_PARAM = 2, /**< Invalid parameters. */
    PROV_RET_INVALID_DATA = 3, /**< Invalid data. */
    PROV_RET_JOINING_LIB_ERROR = 4, /**< Joining library error. */
    PROV_RET_INTERNAL_ERROR = 5, /**< Internal error (no more task, ...). */
} provisioning_ret_e;

/** \brief Provisioning result */
typedef enum
{
    PROV_RES_SUCCESS = 0, /**< Provisioning is a success. */
    PROV_RES_TIMEOUT = 1, /**< Timeout during provisioning. */
    PROV_RES_NACK = 2, /**< Provisioning server returned a NACK. */
    PROV_RES_INVALID_DATA = 3, /**< Received provisioning data is invalid. */
    PROV_RES_INVALID_PACKET = 4, /**< Received packet badly formatted. */
    PROV_RES_ERROR_SENDING_DATA = 5, /**< Problem when sending packet. */
    PROV_RES_ERROR_SCANNING_BEACONS = 6, /**< Error while scanning for joining
                                                                    beacons. */
    PROV_RES_ERROR_JOINING = 7, /**< Error during joining process. */
    PROV_RES_ERROR_NO_ROUTE = 8, /**< No route to host after joining network. */
    PROV_RES_STOPPED = 9, /**< Application called stop function. */
    PROV_RES_ERROR_INTERNAL = 10, /**< Internal error (no more task, ...). */
    PROV_RES_ERROR_FAILSAFE = 11
} provisioning_res_e;

/**
 * \brief   Supported provisioning method bit flags
 */
typedef enum
{
    /**
     * First bit defines whether secured provisioning is used.
     */
    PROV_METHOD_FLAG_SECURED      = 0b0001,

    /**
     * Second bit is set if extended UID is used.
     */
    PROV_METHOD_FLAG_EXTENDED_UID = 0b0010,

    /**
     * Third bit if scalable key management is used.
     */
    PROV_METHOD_FLAG_KEY_MGMT     = 0b0100,
} provisioning_method_flag_e;

/**
 * \brief   Provisioning methods
 */
typedef enum
{
    /**
     * Unsecured provisioning, normal UID.
     */
    PROV_METHOD_UNSECURED = 0x0,

    /**
     * Secure provisioning, normal UID.
     */
    PROV_METHOD_SECURED = PROV_METHOD_FLAG_SECURED,

    /**
     * Secure provisioning, extended UID.
     */
    PROV_METHOD_EXTENDED_UID = PROV_METHOD_FLAG_SECURED |
                               PROV_METHOD_FLAG_EXTENDED_UID,

    /**
     * Secure provisioning, extended UID and scalable key management.
     */
    PROV_METHOD_EXTENDED_UID_KEY_MGMT = PROV_METHOD_FLAG_SECURED      |
                                        PROV_METHOD_FLAG_EXTENDED_UID |
                                        PROV_METHOD_FLAG_KEY_MGMT,
} provisioning_method_e;

/**
 * \brief   Macro to check whether method is unsecured.
 */
#define PROV_METHOD_IS_UNSECURED(x) ((x) == 0)

/**
 * \brief   Macro to check whether method is secured.
 */
#define PROV_METHOD_IS_SECURED(x) (((x) & PROV_METHOD_FLAG_SECURED) != 0)

/**
 * \brief   Macro to check whether secure method uses extended UID
 */
#define PROV_METHOD_IS_EXTENDED_UID(x) (((x) & PROV_METHOD_FLAG_EXTENDED_UID) != 0)

/**
 * \brief   Macro to check whether secure method uses scalable key management.
 */
#define PROV_METHOD_IS_KEY_MGMT(x) (((x) & PROV_METHOD_FLAG_KEY_MGMT) != 0)

/**
 * \brief   The end provisioning callback. This function is called at the end
 *          of the provisioning process.
 * \param   result
 *          Result of the provisioning process.
 * \return  True: Apply received network parameters and reboot; False: discard
 *          data and end provisioning process.
 */
typedef bool (*provisioning_end_cb_f)(provisioning_res_e result);

/**
 * \brief   Received User provisioning data callback.
 *          Provisioning data is received as a map of id:data. This function
 *          is callback for each id that are not reserved by Wirepas.
 * \param   id
 *          Id of the received item.
 * \param   data
 *          Received data.
 * \param   len
 *          Length of the data.
 */
typedef void (*provisioning_user_data_cb_f)(uint32_t id,
                                            CborType type,
                                            uint8_t * data,
                                            uint8_t len);

/**
 * \brief   Selects which joining beacon to connect to at the end of a scan.
 * \note    Joining network selection is up to the application. Any algorithm
 *          can be implemented for example by looking at the content of the
 *          beacon payload, or the network (address and channel) the node
 *          sending the beacons is connected to.
 * \param   beacons
 *          A buffer of joining beacons.
 * \return  The selected beacon.
 */
typedef const app_lib_joining_received_beacon_t *
                    (*provisioning_joining_beacon_cb_f)
                            (const app_lib_joining_received_beacon_t * beacons);

/**
 * \brief This structure contains the network parameters sent by the
 *        provisioning proxy to the new node.
 */
typedef struct
{
    /** The network encryption key. */
    uint8_t enc_key[APP_LIB_SETTINGS_AES_KEY_NUM_BYTES];
    /** The network authentication key. */
    uint8_t auth_key[APP_LIB_SETTINGS_AES_KEY_NUM_BYTES];
    /** The network address. */
    app_lib_settings_net_addr_t net_addr;
    /** The network channel. */
    app_lib_settings_net_channel_t net_chan;
} provisioning_proxy_net_param_t;

/**
 * \brief   The proxy received START packet callback. This function is called
 *          when the proxy receives a valid START packet from a new node.
 * \note    Local provisioning must be activated for the proxy to be able to
 *          receive provisioning packet in the application. Otherwise they
 *          are directly forwarded by the stack to the Sink.
 * \param   uid
 *          A pointer to the node UID.
 * \param   uid_len
 *          The size in bytes of the UID
 * \param   method
 *          The provisioning method requested by the new node.
 * \param   net_param
 *          If returning true, the callback must fill this structure with the
 *          network parameters that will be sent to the new node.
 * \return  true: Send provisioning data to this node; false: discard the node
 *          request and reply with a NACK.
 */
typedef bool (*provisioning_proxy_start_cb_f)(
                                const uint8_t * uid,
                                uint8_t uid_len,
                                provisioning_method_e method,
                                provisioning_proxy_net_param_t * net_param);

typedef union
{
    uint8_t uid[PROV_UID_MAX_SIZE_BYTES];
    struct
    {
        /** Authenticator UID type (used when PROV_METHOD_FLAG_EXTENDED_UID is set in the prov method) */
        uint8_t authenticator_uid_type;
        /** Authenticator UID (used when PROV_METHOD_FLAG_EXTENDED_UID is set in the prov method) */
        uint8_t authenticator_uid[PROV_AUTH_UID_SIZE_BYTES];
        /** Node UID type (used when PROV_METHOD_FLAG_EXTENDED_UID is set in the prov method) */
        uint8_t node_uid_type;
        /** Node UID (used when PROV_METHOD_FLAG_EXTENDED_UID is set in the prov method) */
        uint8_t node_uid[PROV_UUID_SIZE_BYTES];
    };
} provisioning_uid_t;


/**
 * \brief This structure contains the provisioning settings stored in the secure
 *        storage.
 */
typedef struct
{
    /** The provisioning method that the node wants to use. */
    provisioning_method_e method;
    /** How many retries are allowed to receive provisioning data. */
    uint8_t nb_retry;
    /** Timeout in seconds (typ. 10sec for LL and 120sec for LE network) */
    uint16_t timeout_s;
    /** UID of the node*/
    provisioning_uid_t prov_uid;
    /** Length of the UID buffer.*/
    uint8_t prov_uid_len;

    /** Key used for provisioning, [16B AK][16B EK] for Secured method. This
     *  implementation of the provisioning protocol only supports the factory
     *  key.
     */
    struct
    {
        uint8_t auth_key[PROV_KEY_SIZE_BYTES]; /**< Authentication key */
        uint8_t enc_key[PROV_KEY_SIZE_BYTES]; /**< Encryption key */
    } prov_keys;
} provisioning_settings_t;

/**
 * \brief This structure contains all provisioning related parameters.
 */
typedef struct
{
    /** Provisioning settings. */
    provisioning_settings_t settings;
    /** End provisioning callback. */
    provisioning_end_cb_f end_cb;
    /** Data provisioning callback. */
    provisioning_user_data_cb_f user_data_cb;
    /** Beacon joining callback. Needed if use_joining is true. */
    provisioning_joining_beacon_cb_f beacon_joining_cb;
} provisioning_conf_t;

/**
 * \brief This structure holds the joining proxy parameters.
 * \note  Local provisioning is enabled if unsecured or secured method is
 *        allowed. When local provisioning is enabled the provisioning packets
 *        are treated locally by the proxy and are not forwarded anymore to the
 *        Sink / Provisioning server.
 */
typedef struct
{
    /** Joining beacons payload. */
    uint8_t * payload;
    /** Joining beacons payload number of bytes. */
    uint8_t num_bytes;
    /** Transmission power to use for sending joining beacons, in dBm. */
    int8_t tx_power;
    /** Is local unsecured provisioning method allowed. */
    bool is_local_unsec_allowed;
    /** Is local secured provisioning method allowed. */
    bool is_local_sec_allowed;
    /** Key used for provisioning, [16B AK][16B EK] for Secured method. This
     *  implementation of the provisioning protocol only supports the factory
     *  key.
     */
    const uint8_t * key;
    /** Length of the key. Secure method expects 32 bytes keys. */
    uint8_t key_len;
    /** The received START packet callback. */
    provisioning_proxy_start_cb_f start_cb;
} provisioning_proxy_conf_t;

/**
 * \brief   Initialize the provisioning process.
 * \note    If Provisioning is used, App_scheduler and Shared_data MUST BE
 *          initialized in App_Init of the application. Also
 *          [lib_system->setShutdownCb](@ref app_lib_system_set_shutdown_cb_f)()
 *          function offered by system library MUST NOT be used outside of
 *          this module. If Joining is used,
 *          [lib_joining->startJoiningBeaconRx()](@ref app_lib_joining_start_joining_beacon_rx_f) function
 *          offered by Joining library and [lib_state->setRouteCb](@ref app_lib_state_set_route_cb_f)() function offered by
 *          State library MUST NOT be used outside of this module.
 * \note    When using provisioning application must use Shared_data
 *          instead of data library for sending and receiving packets.
 * \param   conf
 *          Configuration for the provisioning. If conf is NULL, the library
 *          only registers the callbacks to receive provisioning configuration
*           over Remote API and then returns with PROV_RET_INVALID_DATA.
 * \return  Result code, \ref PROV_RET_OK if provisioning is initialized.
 *          \ref PROV_RET_INVALID_DATA if called with conf == NULL (to distinguish from other errors)
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_init(provisioning_conf_t * conf);

/**
 * \brief   Start the provisioning process.
 * \return  Result code, \ref PROV_RET_OK if provisioning has started.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_start(void);

/**
 * \brief   Stops the provisioning process.
 * \note    All taken callback, packet received filter and tasks are freed.
 * \note    Stop is not immediate, it takes approximately 100ms.
 * \return  Result code, \ref PROV_RET_OK if provisioning has stopped.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_stop(void);

/**
 * \brief   Initialize the provisioning proxy.
 * \note    If Provisioning Proxy is used, Shared_data MUST BE initialized
 *          in App_Init of the application.
 * \note    If local provisioning is enabled, provisioning request sent by new
 *          node will be treated locally by this library instead of being
 *          forwarded to the provisioning server.
 * \param   conf
 *          Configuration for the provisioning proxy.
 * \return  Result code, \ref PROV_RET_OK if proxy is initialized.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_Proxy_init(provisioning_proxy_conf_t * conf);

/**
 * \brief   Start sending joining beacons. Provisioning packets will be
 *          forwarded to provisioning server or treated locally if local
 *          provisioning is enabled.
 * \return  Result code, \ref PROV_RET_OK if proxy has started.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_Proxy_start(void);

/**
 * \brief   Stops the provisioning proxy.
 * \note    All packet filter callbacks are freed.
 * \return  Result code, \ref PROV_RET_OK if proxy has stopped.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_Proxy_stop(void);

/**
 * \brief   Writes provisioning settings to the secure storage.
 * \param   settings
 *          Pointer to provisioning settings structure to be written to the
 *          secure storage.
 * \return  Result code, \ref PROV_RET_OK if configuration has been written
 *          successfully, \ref PROV_RET_INVALID_PARAM otherwise.
 */
provisioning_ret_e Provisioning_settings_write(const provisioning_settings_t * settings);

/**
 * \brief   Reads provisioning settings from the secure storage.
 * \param   settings
 *          Pointer to provisioning settings structure for storing read
 *          configuration.
 * \return  Result code, \ref PROV_RET_OK if configuration has been read
 *          successfully, \ref PROV_RET_INVALID_PARAM otherwise.
 */
provisioning_ret_e Provisioning_settings_read(provisioning_settings_t * settings);

/**
 * \brief   Initialize the provisioning process with provisioning settings
 *          from the secure storage.
 * \param   conf
 *          Configuration defining callback functions for provisioning.
 * \return  Result code, \ref PROV_RET_OK if provisioning is initialized.
 *          See \ref provisioning_ret_e for other return codes.
 */
provisioning_ret_e Provisioning_init_from_storage(provisioning_conf_t * conf);

#endif //_PROVISIONING_H_
