/* Copyright 2024 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */
#include "app_secure_storage.h"
#include "api.h"

app_secure_storage_res_e App_SecureStorage_init(uint32_t area_id)
{
    return (app_secure_storage_res_e) lib_secure_storage->initArea(
        area_id,
        APP_LIB_SECURE_STORAGE_CMD_INIT,
        0);
}

app_secure_storage_res_e App_SecureStorage_erase(uint32_t area_id)
{
    return (app_secure_storage_res_e) lib_secure_storage->initArea(
        area_id,
        APP_LIB_SECURE_STORAGE_CMD_ERASE,
        0);
}

app_secure_storage_res_e App_SecureStorage_check(uint32_t area_id)
{
    return (app_secure_storage_res_e) lib_secure_storage->initArea(
        area_id,
        APP_LIB_SECURE_STORAGE_CMD_NO_INIT,
        0);
}

app_secure_storage_res_e App_SecureStorage_write(
    uint32_t area_id, uint32_t tag, const uint8_t * data, size_t len,
    app_secure_storage_flags_e flags)
{
    return (app_secure_storage_res_e)
        lib_secure_storage->writeRecord(area_id, tag, data, len, flags);
}

app_secure_storage_res_e App_SecureStorage_read(uint32_t area_id, uint32_t tag,
                                                uint8_t * data,
                                                size_t *  max_amount)
{
    return (app_secure_storage_res_e)
        lib_secure_storage->readRecord(area_id, tag, data, max_amount);
}

app_secure_storage_res_e App_SecureStorage_delete(uint32_t area_id,
                                                  uint32_t tag)
{
    return (app_secure_storage_res_e) lib_secure_storage->deleteRecord(area_id,
                                                                       tag);
}
