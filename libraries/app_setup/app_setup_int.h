/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#ifndef APP_SETUP_INT_H
#define APP_SETUP_INT_H
/**
 * @brief   Internal header for application setup library
 *
 *          Data structures match the output produced by:
 *
 *              tools/app_setup.py
 *
 *  @note   Data structures are packed and aligned so that every compound
 *          member and array are word addressable.
 *
 *          One should not use scalar members as pointers, because they are
 *          not guaranteed to be on word addressable offset.
 */

/**
 * DEFINITIONS
 */

/**
 * @brief   Application setup data identifier
 *
 *          If application persistent area has this at the beginning, the
 *          data is presumed to be application setup data.
 */
#define APP_SETUP_FORMAT 0xabb5e70b

/**
 * @brief   Version of the library
 */
#define APP_SETUP_VERSION 2

/**
 * @brief   In flash 0xff marks empty byte
 */
#define APP_SETUP_CLEAR_BYTE 0xff

/**
 * @brief   All security keys are 16 bytes long
 */
#define APP_SETUP_KEY_SIZE_BYTES 16

/**
 * @brief   Maximum size of provisioning UID
 */
#define APP_SETUP_PROV_UID_MAX_SIZE_BYTES 79

/**
 * @brief   Authenticator UID size
 */
#define APP_SETUP_AUTH_UID_SIZE_BYTES 16

/**
 * TYPES
 */

/**
 * @brief   Application setup data header
 */
typedef struct __attribute__((__packed__))
{
    /**
     * APP_SETUP_FORMAT
     */
    uint32_t format;

    /**
     * APP_SETUP_VERSION
     */
    uint16_t version;

    /**
     * Length in bytes of header + data
     */
    uint16_t length;

} app_setup_hdr_t;

/**
 * @brief   Node configuration
 */
typedef struct __attribute__((__packed__))
{
    /**
     * Node id
     */
    uint32_t id;

    /**
     * Node role
     */
    uint8_t role;
    /**
     * Diagnostic interval in seconds
     */
    uint16_t diag_interval;

    uint8_t reserved;

} app_setup_node_t;

/**
 * @brief   Key management configuration
 */
typedef struct __attribute__((__packed__))
{

    /**
     * Network address
     */
    uint32_t network_address;

    /**
     * Network channel
     */
    uint8_t network_channel;

    /**
     * Network key pair sequence
     */
    uint8_t network_key_pair_seq;

    /**
     * Management key pair sequence
     */
    uint8_t mgmt_key_pair_seq;

    uint8_t reserved;

    /**
     * Network encryption key
     */
    uint8_t network_enc_key[APP_SETUP_KEY_SIZE_BYTES];

    /**
     * Network authentication key
     */
    uint8_t network_auth_key[APP_SETUP_KEY_SIZE_BYTES];

    /**
     * Management encryption key
     */
    uint8_t mgmt_enc_key[APP_SETUP_KEY_SIZE_BYTES];

    /**
     * Management authentication key
     */
    uint8_t mgmt_auth_key[APP_SETUP_KEY_SIZE_BYTES];

} app_setup_network_t;

/**
 * @brief   Provisioning configuration
 */
typedef struct __attribute__((__packed__))
{
    /**
     * Provisioning method
     */
    uint8_t method;

    /**
     * How many retries will be attempted
     */
    uint8_t num_retries;

    /**
     * Timeout in seconds
     */
    uint16_t timeout;

    /**
     * Encryption key if method is SECURED
     */
    uint8_t enc_key[APP_SETUP_KEY_SIZE_BYTES];

    /**
     * Authentication key if method is SECURED
     */
    uint8_t auth_key[APP_SETUP_KEY_SIZE_BYTES];

    /**
     * Provisioning UID
     */
    uint8_t prov_uid[APP_SETUP_PROV_UID_MAX_SIZE_BYTES];

    /**
     * Provisioning UID length
     */
    uint8_t prov_uid_len;

} app_setup_provisioning_t;

/**
 * @brief   Actions for application setup
 */
typedef struct __attribute__((__packed__))
{
    /**
     * Start dualMCU app on boot without explicit command
     */
    uint8_t start_dualmcu;

    /**
     * Preserved data in app persistent area after first boot
     */
    uint8_t preserve_data;

    /**
     * Reserved to align structure to word
     */
    uint16_t reserved;

} app_setup_action_t;

/**
 * @brief   Application setup data structure
 */
typedef struct __attribute__((__packed__)) app_setup
{
    /**
     * Header
     */
    app_setup_hdr_t header;

    /**
     * Node configuration
     */
    app_setup_node_t node;

    /**
     * Network configuration
     */
    app_setup_network_t network;

    /**
     * Provisioning configuration
     */
    app_setup_provisioning_t provisioning;

    /**
     * Application setup actions
     */
    app_setup_action_t action;

} app_setup_t;

#endif // APP_SETUP_INT_H
