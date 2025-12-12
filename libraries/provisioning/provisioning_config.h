#ifndef _PROVISIONING_CONFIG_H_
#define _PROVISIONING_CONFIG_H_

#include "api.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Function for handling the Remote API CSAP attribute write requests on the
 * application side. Stores the CSAP attribute values to a temporary location
 * and builds the response.
 * See, more details on the use of the write function in the Remote
 * API reference manual.
 * \param attr_id   Attribute ID value that is written. Remote API is expected
 *                  to call this function only with an attribute ID between
 *                  0xC001 - 0xFFFF.
 * \param flags     Includes additional information such as whether the write
 *                  requestwas transported in a unicast packet.
 * \param data      The payload content of the CSAP attribute write request as
 *                  well as reference to buffers where the response is written.
 * \return          Returns REMOTE_API_CSAP_RESPONSE_NONE is write was succesful
 *                  and response was generated successfully, otherwise an error
 *                  code is returned.
 */
uint8_t remote_api_csap_write( uint16_t attr_id, uint32_t flags, app_lib_settings_remote_api_csap_data_t * data);

/**
 * Function for handling the Remote API CSAP attribute read requests on the
 * application side. Reads the CSAP attribute values and builds the response.
 * See, more details on the use of the write function in the Remote
 * API reference manual.
 * \param attr_id   Attribute ID value that is read. Remote API is expected
 *                  to call this function only with an attribute ID between
 *                  0xC001 - 0xFFFF.
 * \param data      The payload content of the CSAP attribute read request as
 *                  well as reference to buffers where the response is written.
 * \return          Returns REMOTE_API_CSAP_RESPONSE_NONE is read was succesful
 *                  and response was generated successfully, otherwise an error
 *                  code is returned.
 */
uint8_t remote_api_csap_read( uint16_t attr_id, app_lib_settings_remote_api_csap_data_t * data);

/**
 * Function for handling the Remote API CSAP attribute update request on
 * application side. Stores the CSAP attribute values of the preceding write
 * requests to a persistent location (from the temporary location). See, more
 * details on the purpose and use of the update function in the Remote API
 * reference manual.
 * \return  Return APP_LIB_SETTINGS_REMOTE_API_RES_OK if update was ok,
 *          APP_LIB_SETTINGS_REMOTE_API_RES_OK_RESET if update was ok and reset
 *          is required, APP_LIB_SETTINGS_REMOTE_API_RES_NONE otherwise.
 */
app_lib_settings_remote_api_res_e remote_api_csap_update(void);

/**
 * Function for handling the Remote API CSAP attribute cancel request on
 * application side. Clear any stored attributes from the temporary location
 * which were stored there in write requests. See, more details on the
 * purpose and use of the cancel function in the Remote API reference manual.
 */
void remote_api_csap_cancel(void);

#endif // _PROVISIONING_CONFIG_H_
