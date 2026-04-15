/* Copyright 2017 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/*
 * \file    app.c
 * \brief   This file is a template to Dual MCU API app for all paltforms
 */
#include <stdlib.h>
#include <api.h>

#include "app_setup.h"

#ifdef DUALMCU_APP_KEY_MGMT
#include "wms_settings.h"
#include "provisioning.h"
#endif

#ifndef DUALMCU_APP_KEY_MGMT
#include "local_provisioning.h"
#endif
#include "dualmcu_lib.h"

/**
 * \brief   Initialization callback for application
 *
 * This function is called after hardware has been initialized but the
 * stack is not yet running.
 *
 */
void App_init(const app_global_functions_t * functions)
{
    (void) functions;

    App_Setup();

#ifdef DUALMCU_APP_KEY_MGMT
    /**
     * Provisioning init enables key management, but if the role is sink,
     * we don't use provisioning in the node but set key management enabled
     * here.
     */
    app_lib_settings_role_t role;
    if (lib_settings->getNodeRole(&role) == APP_RES_OK)
    {
        if (role == APP_LIB_SETTINGS_ROLE_SINK_LE
            || role == APP_LIB_SETTINGS_ROLE_SINK_LL)
        {
            lib_settings->keyManagementConfiguration(
                &(app_lib_settings_key_management_configuration_t){
                    .flags = {
                        .apply_flags = 1,
                        .app_key_management_supported = 1,
                        .app_key_management_configured = 1
                    }
                }
            ); 
        } 
        else
        {
            Provisioning_init_from_storage(&(provisioning_conf_t){0});
        }
    }
#else
    Local_provisioning_init(NULL, NULL);
#endif

    Dualmcu_lib_init(UART_BAUDRATE, UART_FLOWCONTROL);
}
