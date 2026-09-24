/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/*
 * \file    storage.h
 * \brief   This file provides interface to read provisioning parameters
 *          from secure storage. Use app_setup.py to store the parameters
 *          in the secure storage.
 */
#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "provisioning.h"

/**
 * \brief   Initialize the storage module.
 * \note    Select storage method (chipid or memarea) by setting storage
 *          variable in config.mk.
 * \return  false if an error occurred, true otherwise.
 */
bool Storage_init();

/**
 * \brief   Get UID.
 * \param   uid
 *          A pointer for storing the UID.
 * \return  The length of the UID.
 * \note    Returning an int8_t is ok because UID fit in a 102 bytes packet.
 */
int8_t Storage_getUID(uint8_t * uid);

/**
 * \brief   Get provisioning authentication key.
 * \param   key
 *          A pointer for storging the authentication key.
 * \return  The length of the read authentication key.
 * \note    Returning an int8_t is ok because KEY fit in a 102 bytes packet.
 */
int8_t Storage_getAuthKey(uint8_t * key);

/**
 * \brief   Get provisioning encryption key.
 * \param   key
 *          A pointer for storging the encryption key.
 * \return  The length of the read encryption key.
 * \note    Returning an int8_t is ok because KEY fit in a 102 bytes packet.
 */
int8_t Storage_getEncKey(uint8_t * key);

/**
 * \brief   Get the provisioning method.
 * \return  The provisioning method.
 */
provisioning_method_e Storage_getMethod();

/**
 * \brief   Get the node UID type for Extended UID method.
 * \return  The node UID type, or 0 if not Extended UID method.
 */
uint8_t Storage_getNodeUidType();

/**
 * \brief   Get a pointer to the node UID for Extended UID method.
 * \param   node_uid
 *          A pointer for storing the node UID.
 * \return  The length of the node UID, or -1 if not Extended UID method.
 */
int8_t Storage_getNodeUID(uint8_t * node_uid);

/**
 * \brief   Get the authenticator UID type for Extended UID method.
 * \return  The authenticator UID type, or 0 if not Extended UID method.
 */
uint8_t Storage_getAuthenticatorUidType();

/**
 * \brief   Get a pointer to the authenticator UID for Extended UID method.
 * \param   auth_uid
 *          A pointer for storing the authenticator UID.
 * \return  The length of the authenticator UID, or -1 if not Extended UID method.
 */
int8_t Storage_getAuthenticatorUID(uint8_t * auth_uid);

#endif //_STORAGE_H_
