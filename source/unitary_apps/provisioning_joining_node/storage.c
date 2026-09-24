/*
 * storage.c
 *
 * Copyright 2019,2025
 * Author: Wirepas Ltd
 *
 * See file LICENSE.txt for full license details.
 *
 */

/******************************************************************************
 * @file storage.c
 * @brief This file provides interface to read provisioning settings from
 *         the secure storage.
 ******************************************************************************/
/******************************************************************************
 * INCLUDES
 ******************************************************************************/
#include "storage.h"
#include "api.h"
#define DEBUG_LOG_MODULE_NAME "STORAGE "
#define DEBUG_LOG_MAX_LEVEL LVL_NOLOG
#include "debug_log.h"

/******************************************************************************
 * DEFINES
 ******************************************************************************/
#define EXTENDED_UID_TYPE_SIZE 1
#define EXTENDED_UID_UUID_SIZE 16
#define AUTHENTICATOR_UID_TYPE_SIZE EXTENDED_UID_TYPE_SIZE
#define AUTHENTICATOR_UID_SIZE EXTENDED_UID_UUID_SIZE
#define NODE_UID_TYPE_SIZE EXTENDED_UID_TYPE_SIZE
#define NODE_UID_SIZE EXTENDED_UID_UUID_SIZE
#define EXTENDED_UID_TOTAL_SIZE (AUTHENTICATOR_UID_TYPE_SIZE + AUTHENTICATOR_UID_SIZE + NODE_UID_TYPE_SIZE + NODE_UID_SIZE)

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/
/* Provisioning settings */
 static provisioning_settings_t m_prov_settings;

/******************************************************************************
 * PRIVATE FUNCTIONS
 ******************************************************************************/

/******************************************************************************
 * PUBLIC FUNCTIONS
 ******************************************************************************/
bool Storage_init()
{
    /* Read Provisioning settings from the storage */
    memset(&m_prov_settings, 0, sizeof(provisioning_settings_t));
    if (Provisioning_settings_read(&m_prov_settings) != PROV_RET_OK)
    {
        LOG(LVL_ERROR, "Error reading provisioning settings");
        return false;
    }

    return true;
}

int8_t Storage_getUID(uint8_t * uid)
{
    memcpy(uid, &m_prov_settings.prov_uid.uid, m_prov_settings.prov_uid_len);
    return m_prov_settings.prov_uid_len;
}

int8_t Storage_getAuthKey(uint8_t * key)
{
    memcpy(key, &m_prov_settings.prov_keys.auth_key[0], PROV_KEY_SIZE_BYTES);
    return PROV_KEY_SIZE_BYTES;
}

int8_t Storage_getEncKey(uint8_t * key)
{
    memcpy(key, &m_prov_settings.prov_keys.enc_key, PROV_KEY_SIZE_BYTES);
    return PROV_KEY_SIZE_BYTES;
}

provisioning_method_e Storage_getMethod()
{
    return m_prov_settings.method;
}

uint8_t Storage_getNodeUidType()
{
    assert(PROV_METHOD_IS_EXTENDED_UID(m_prov_settings.method));
    return m_prov_settings.prov_uid.node_uid_type;
}

int8_t Storage_getNodeUID(uint8_t * node_uid)
{
    assert(PROV_METHOD_IS_EXTENDED_UID(m_prov_settings.method));
    memcpy(node_uid, &m_prov_settings.prov_uid.node_uid, EXTENDED_UID_UUID_SIZE);
    return EXTENDED_UID_UUID_SIZE;
}

uint8_t Storage_getAuthenticatorUidType()
{
    assert(PROV_METHOD_IS_EXTENDED_UID(m_prov_settings.method));
    return m_prov_settings.prov_uid.authenticator_uid_type;
}

int8_t Storage_getAuthenticatorUID(uint8_t * auth_uid)
{
    assert(PROV_METHOD_IS_EXTENDED_UID(m_prov_settings.method));
    memcpy(auth_uid, &m_prov_settings.prov_uid.authenticator_uid, EXTENDED_UID_UUID_SIZE);
    return EXTENDED_UID_UUID_SIZE;
}
