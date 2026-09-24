
#include "provisioning_config.h"
#include "provisioning.h"
#include "crc.h"
#include "util.h"

#include <string.h>
#include <stddef.h>

enum attr_id
{
    ATTR_ID_PROVISIONING_CONFIG = 0xC001,
};

/** Internal status bits. Bits are set and cleared on
 * calls of the write callback. Bits are cleared on
 * update and cancel callbacks.
 * Bit 0: Set when ATTR_ID_PROVISIONING_CONFIG write request is
 *        received and write is successful, cleared when cancel
 *        or update is called.
 *
 * Bits 1-31: reserved for future extensions
 *
 */
static uint16_t m_status;

/** Provision config write received bit, see \ref m_status for more info */
#define STATUS_PROVISIONING_CONFIG_WRITE 0x1

/** UID type for UUID version 4 */
#define PROV_UID_TYPE_UUID_V4 0x01

/**
 * Overlay struct for the ATTR_ID_PROVISIONING_CONFIG write
 * request packet contents.
 */
struct __attribute__((packed)) provisioning_config_req
{
    /** Provisioning method */
    uint8_t  method;
    /** Number of retries of attempting joing beacons scan.*/
    uint8_t  retry;
    /** Timeout */
    uint16_t timeout;
    /** Encryption key for encrypting provisioning data packets with AES128 CTR */
    uint8_t  enc_key[16];
    /** Authentication key for authenticating provisioning data packets with OMAC */
    uint8_t  auth_key[16];
    /** Length of the uid field, valid values 1-79 */
    uint8_t  uid_len;
    /** UID of the node, maximum size 79 bytes */
    uint8_t  uid[79];
};

/** The number of bytes taken by the fields until the uid array. */
#define PROVISIONING_CONFIG_FIXED_SIZE offsetof(struct provisioning_config_req, uid)

// Check that the struct member sizes are equal, otherwise the values may not fit when doing a copy.
_Static_assert( (member_size(struct provisioning_config_req, uid) == member_size(provisioning_uid_t, uid)),
                "Struct members \"uid\" must be of equal size");

/**
 * Overlay struct for the ATTR_ID_PROVISIONING_CONFIG write/read
 * response packet contents.
 */
struct __attribute__((packed)) provisioning_config_resp
{
    /** Provisioning method */
    uint8_t  method;
    /** Number of retries of attempting joing beacons scan.*/
    uint8_t  retry;
    /** Timeout */
    uint16_t timeout;
    /** CRC16 checksum for the encryption and authentication key */
    uint16_t keys_crc16;
    /** Length of the uid field, valid values 1-79 */
    uint8_t  uid_len;
    /** UID of the node, maximum size 79 bytes */
    uint8_t  uid[79];
};

#define PROVISIONING_CONFIG_RESP_FIXED_SIZE offsetof(struct provisioning_config_resp, uid)

/** Temporary storage for the provisioning config data between the write and update calls */
static struct provisioning_config_req m_provisioning_config;

/** Helper function for writing the provisioning config and building the response. */
static uint8_t write_provisioning_config(app_lib_settings_remote_api_csap_data_t * data);

/** Helper function for writing the response. Both CSAP write and CSAP read request are responded with
 * same response.
 */
static void write_resp_values(struct provisioning_config_resp * resp, struct provisioning_config_req * config);

/** Helper function for reading the provisioning config and building the response. */
static uint8_t read_provisioning_config(app_lib_settings_remote_api_csap_data_t * data);

uint8_t remote_api_csap_write( uint16_t attr_id, uint32_t flags, app_lib_settings_remote_api_csap_data_t * data)
{
    uint8_t res;
    switch (attr_id)
    {
        case ATTR_ID_PROVISIONING_CONFIG:
            // PROVISIONING_CONFIG must be sent in an unicast data packet, otherwise many nodes in a
            // network end up having the same Node UID and Provisioning key pair
            if (!(flags & REMOTE_API_CSAP_WRITE_UNICAST))
            {
                res = REMOTE_API_CSAP_RESPONSE_INVALID_BROADCAST_REQUEST;
                break;
            }

            res = write_provisioning_config(data);
            if (res == REMOTE_API_CSAP_RESPONSE_NONE)
            {
                m_status |= STATUS_PROVISIONING_CONFIG_WRITE;
            }
            break;
        default:
            res = REMOTE_API_CSAP_RESPONSE_UNKNOWN_REQUEST;
            break;
    }

    return res;
}

static settings_validation_ret_e populate_provisioning_settings(
    const struct provisioning_config_req *req,
    provisioning_settings_t *settings)
{
    settings_validation_ret_e ret = SETTINGS_OK;

    settings->method = req->method;
    settings->nb_retry = req->retry;
    settings->timeout_s = req->timeout;

    uint8_t length = req->uid_len;

    if (length > sizeof(settings->prov_uid.uid))
    {
        length = sizeof(settings->prov_uid.uid);
        ret = SETTINGS_UID_INVALID_LENGTH;
    }

    settings->prov_uid_len = length;
    memcpy(settings->prov_uid.uid,
           req->uid,
           length);
    memcpy(settings->prov_keys.enc_key,
           req->enc_key,
           sizeof(settings->prov_keys.enc_key));
    memcpy(settings->prov_keys.auth_key,
           req->auth_key,
           sizeof(settings->prov_keys.auth_key));
    
    return ret;
}

static uint8_t write_provisioning_config(app_lib_settings_remote_api_csap_data_t * data)
{
    // Check the data length in the request buffer
    if (data->request_buffer_len < PROVISIONING_CONFIG_FIXED_SIZE)
    {
        // Return an error if minimum data amount is not received
        return REMOTE_API_CSAP_RESPONSE_INVALID_LENGTH;
    }

    struct provisioning_config_req * temp_config
        = (struct provisioning_config_req *)data->request_buffer;
    uint8_t uid_len = temp_config->uid_len;
    if (data->request_buffer_len != PROVISIONING_CONFIG_FIXED_SIZE + uid_len)
    {
        // Return an error if the uid received does not have a length within accepted limits
        return REMOTE_API_CSAP_RESPONSE_INVALID_LENGTH;
    }

    // Check that the response buffer has enough space to write the response
    if (data->response_buffer_len < offsetof(struct provisioning_config_resp, uid) + uid_len)
    {
        return REMOTE_API_CSAP_RESPONSE_NO_SPACE_FOR_RESPONSE;
    }

    // Validate settings
    provisioning_settings_t settings;
    settings_validation_ret_e validation_result = populate_provisioning_settings(temp_config, &settings);
    
    if (validation_result == SETTINGS_OK)
    {
        validation_result = validate_settings(&settings);
    }
    
    switch (validation_result)
    {
        case SETTINGS_OK:
            break;
        case SETTINGS_UID_INVALID_LENGTH:
            return REMOTE_API_CSAP_RESPONSE_INVALID_LENGTH;
        default:
            return REMOTE_API_CSAP_RESPONSE_INVALID_VALUE;
    }

    // Copy request contents to a temporary location
    memcpy(&m_provisioning_config, temp_config,
        PROVISIONING_CONFIG_FIXED_SIZE + uid_len);

    // Write the response
    struct provisioning_config_resp * resp
        = (struct provisioning_config_resp *)data->response_buffer;
    write_resp_values(resp, &m_provisioning_config);
    data->response_buffer_len = PROVISIONING_CONFIG_RESP_FIXED_SIZE + resp->uid_len;

    return REMOTE_API_CSAP_RESPONSE_NONE;
}

uint8_t remote_api_csap_read( uint16_t attr_id, app_lib_settings_remote_api_csap_data_t * data)
{
    uint8_t res;
    switch (attr_id)
    {
        case ATTR_ID_PROVISIONING_CONFIG:
            res = read_provisioning_config(data);
            break;
        default:
            res = REMOTE_API_CSAP_RESPONSE_UNKNOWN_REQUEST;
            break;
    }

    return res;
}

static uint8_t read_provisioning_config(app_lib_settings_remote_api_csap_data_t * data)
{
    // Check the data length in the request buffer
    if (data->request_buffer_len != 0)
    {
        // Return an error if the request has any data
        return REMOTE_API_CSAP_RESPONSE_INVALID_LENGTH;
    }

    // Write the response
    provisioning_settings_t prov_settings;
    provisioning_ret_e ret = Provisioning_settings_read(&prov_settings);

    if (ret != PROV_RET_OK)
    {
        return REMOTE_API_CSAP_RESPONSE_INVALID_VALUE;
    }

    uint8_t uid_len = prov_settings.prov_uid_len;

    // There quite a much unnecessary copying of data from temporary buffer to another here
    struct provisioning_config_resp * resp
        = (struct provisioning_config_resp *)data->response_buffer;
    struct provisioning_config_req provisioning_config =
        {
            .method  = prov_settings.method,
            .retry   = prov_settings.nb_retry,
            .timeout = prov_settings.timeout_s,
            .uid_len = uid_len,
        };

    // Check that the temporary variable and response buffer has enough space for writing
    if (uid_len > sizeof(provisioning_config.uid)
        || data->response_buffer_len < offsetof(struct provisioning_config_resp, uid) + uid_len)
    {
        return REMOTE_API_CSAP_RESPONSE_NO_SPACE_FOR_RESPONSE;
    }

    memcpy(provisioning_config.enc_key, prov_settings.prov_keys.enc_key,
        sizeof(provisioning_config.enc_key));
    memcpy(provisioning_config.auth_key, prov_settings.prov_keys.auth_key,
        sizeof(provisioning_config.auth_key));
    memcpy(provisioning_config.uid, prov_settings.prov_uid.uid,
        prov_settings.prov_uid_len);

    write_resp_values(resp, &provisioning_config);

    data->response_buffer_len = PROVISIONING_CONFIG_RESP_FIXED_SIZE + resp->uid_len;

    return REMOTE_API_CSAP_RESPONSE_NONE;
}

static void write_resp_values(struct provisioning_config_resp * resp, struct provisioning_config_req * config)
{
    if (resp == NULL || config == NULL)
    {
        return;
    }
    resp->method = config->method;
    resp->retry = config->retry;
    resp->timeout = config->timeout;
    resp->keys_crc16 = Crc_fromBuffer((uint8_t *)config->enc_key,
        sizeof(config->enc_key)+sizeof(config->auth_key));
    resp->uid_len = config->uid_len;
    memcpy( &resp->uid,
            &config->uid,
            config->uid_len);
}

app_lib_settings_remote_api_res_e remote_api_csap_update(void)
{

    app_lib_settings_remote_api_res_e result = APP_LIB_SETTINGS_REMOTE_API_RES_NONE;

    bool reset_needed = false;

    if (m_status & STATUS_PROVISIONING_CONFIG_WRITE)
    {
        provisioning_settings_t settings;
        settings_validation_ret_e validation_result = 
        populate_provisioning_settings(&m_provisioning_config, &settings);

        if (validation_result == SETTINGS_UID_INVALID_LENGTH)
        {
            // no can do
            return result;
        }

        provisioning_ret_e res = Provisioning_settings_write(&settings);

        if(res != PROV_RET_OK)
        {
            // handle error
        }
        reset_needed = true;
        // clear m_provisioning_config
        memset(&m_provisioning_config, 0, sizeof(m_provisioning_config));
    }


    // reset status bits
    m_status = 0;

    if (reset_needed)
    {
        result = APP_LIB_SETTINGS_REMOTE_API_RES_OK_RESET;
    }

    return result;
}

void remote_api_csap_cancel(void)
{
    // Clear the temporary variable m_provisioning_config and status bits

    memset(&m_provisioning_config,0,sizeof(m_provisioning_config));
    m_status = 0;
}

/**
 * \brief   Constant‑time equality check over a buffer against a single byte.
 * \note    No early exits; single pass; branch‑stable to avoid timing side channels.
 * \param   buf     Pointer to the input buffer.
 * \param   len     Number of bytes to check.
 * \param   target  Byte value to compare every element against.
 * \return  true if every byte in [buf, len] equals \p target; false otherwise.
 */
static bool is_all_equal_byte(const uint8_t *buf, size_t len, uint8_t target)
{
    uint8_t diff_or = 0;

    for (size_t i = 0; i < len; ++i)
    {
        diff_or |= (uint8_t)(buf[i] ^ target);
    }

    return (diff_or == 0);
}


/**
 * \brief   Validate that the extended UID length and content are acceptable.
 * \note    Requirements:
 *          - Length must match extended UID layout: 2 type bytes + 16 + 16 = 34.
 *          - UID types must be UUIDv4 for both authenticator and node.
 * \param   s   Provisioning settings containing the UID and metadata.
 */
static settings_validation_ret_e validate_extended_uid(
    const provisioning_settings_t *s)
{
    const uint8_t expected_len =(uint8_t)
        (2 + // types
         PROV_AUTH_UID_SIZE_BYTES +
         PROV_UUID_SIZE_BYTES); // = 34 bytes

    if (s->prov_uid_len != expected_len)
    {
        return SETTINGS_UID_INVALID_LENGTH;
    }

    // Check UID types.
    if (s->prov_uid.authenticator_uid_type != PROV_UID_TYPE_UUID_V4 ||
        s->prov_uid.node_uid_type != PROV_UID_TYPE_UUID_V4) {
        return SETTINGS_EUID_INVALID_TYPE;
    }

    return SETTINGS_OK;
}

/**
 * \brief   Validate that both provisioning keys (AK and EK) are non‑trivial.
 * \note    Each key is fixed length (PROV_KEY_SIZE_BYTES). Reject keys that are
 *          all 0x00 or all 0xFF.
 * \param   s   Provisioning settings containing the keys.
 */
static settings_validation_ret_e validate_provisioning_keys(
    const provisioning_settings_t *s)
{
    if (is_all_equal_byte(s->prov_keys.auth_key, PROV_KEY_SIZE_BYTES, 0x00) ||
        is_all_equal_byte(s->prov_keys.auth_key, PROV_KEY_SIZE_BYTES, 0xFF) ||
        is_all_equal_byte(s->prov_keys.enc_key, PROV_KEY_SIZE_BYTES, 0x00) ||
        is_all_equal_byte(s->prov_keys.enc_key, PROV_KEY_SIZE_BYTES, 0xFF))
    {
        return SETTINGS_INVALID_KEY;
    }
    else {
        return SETTINGS_OK;
    }
}


/**
 * \brief   Validate a provisioning settings structure.
 * \note    Checks performed:
 *          - Non‑NULL pointer.
 *          - UID length within [1, PROV_UID_MAX_SIZE_BYTES].
 *          - Extended UID validity when method requires it.
 *          - Non‑zero timeout.
 *          - Provisioning keys validity when method is secured.
 * \param   settings    Pointer to provisioning settings.
 */
settings_validation_ret_e validate_settings(
    const provisioning_settings_t* settings)
{
    if (NULL == settings)
    {
        return SETTINGS_INVALID;
    }

    // UID min, max bounds
    if (settings->prov_uid_len == 0 ||
        settings->prov_uid_len > PROV_UID_MAX_SIZE_BYTES)
    {
        return SETTINGS_UID_INVALID_LENGTH;
    }

    // Valid extended UID?
    settings_validation_ret_e v = validate_extended_uid(settings);
    if (PROV_METHOD_IS_EXTENDED_UID(settings->method) &&
        v != SETTINGS_OK) {
        return v;
    }

    // Timeout ok?
    if (settings->timeout_s == 0)
    {
        return SETTINGS_INVALID_TIMEOUT;
    }

    // Valid keys, if needed?
    if (PROV_METHOD_IS_SECURED(settings->method)) {
        v = validate_provisioning_keys(settings);
        if (v != SETTINGS_OK) {
           return v;
        }
    }

    return SETTINGS_OK;
}
