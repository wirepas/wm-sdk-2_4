/* Copyright 2019 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

/**
 * \file wms_settings.h
 *
 * The Settings library provides access to node settings, which are stored in
 * nonvolatile memory. When a node starts up it automatically uses these stored
 * settings.
 *
 * Settings such as node role, unique node address, network address and channel,
 * encryption and authentication keys as well performance-related settings such
 * as access cycle limits can be stored and recalled. Also see the State library
 * @ref wms_state.h for starting and stopping the stack.
 *
 * Library services are accessed via [lib_settings](@ref app_lib_settings_t)
 * handle.
 */
#ifndef APP_LIB_SETTINGS_H_
#define APP_LIB_SETTINGS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "wms_app.h"

/** @brief Library symbolic name */
#define APP_LIB_SETTINGS_NAME 0x74ced676  //!< "SETTIN"

/** @brief Maximum supported library version */
#define APP_LIB_SETTINGS_VERSION 0x20d

/**
 * @brief AES key size in bytes
 *
 * This macro can be used as a buffer size for storing or copying a 128-bit AES
 * key, or the 16-byte feature lock key. The feature lock key is not an AES-128
 * key, but it is guaranteed to be the same size as an AES-128 key.
 */
#define APP_LIB_SETTINGS_AES_KEY_NUM_BYTES 16

// Device address definition is in app/app.h */

/**
 * @brief Network address type definition.
 *
 * All nodes on the network must have the same network address.
 */
typedef uint32_t app_lib_settings_net_addr_t;

/**
 * @brief Channel type definition.
 */
typedef uint8_t app_lib_settings_net_channel_t;

/**
 * @brief   Reserved value for definition of 'no channel' for @ref
 *          app_lib_settings_net_channel_t
 */
#define CHANNEL_NO_CHANNEL 0

typedef enum
{
    /** Sink in Low Energy mode */
    APP_LIB_SETTINGS_ROLE_SINK_LE = 0x00,
    /** Sink in Low Latency mode */
    APP_LIB_SETTINGS_ROLE_SINK_LL = 0x10,
    /** Headnode in Low Energy mode */
    APP_LIB_SETTINGS_ROLE_HEADNODE_LE = 0x01,
    /** Headnode in Low Latency mode */
    APP_LIB_SETTINGS_ROLE_HEADNODE_LL = 0x11,
    /** Subnode in Low Energy mode */
    APP_LIB_SETTINGS_ROLE_SUBNODE_LE = 0x02,
    /** Subnode in Low Latency mode */
    APP_LIB_SETTINGS_ROLE_SUBNODE_LL = 0x12,
    /** Autorole in Low Energy mode */
    APP_LIB_SETTINGS_ROLE_AUTOROLE_LE = 0x42,
    /** Autorole Low Latency mode */
    APP_LIB_SETTINGS_ROLE_AUTOROLE_LL = 0x52,
    /** Advertiser (implicitly Low Energy) */
    APP_LIB_SETTINGS_ROLE_ADVERTISER = 0x04,
} app_lib_settings_role_e;


/**
 * This is the most common return type from library functions. Functions use
 * these return values, unless more specific return values are required.
 */
typedef enum
{
    /** Nothing was updated */
    APP_LIB_SETTINGS_REMOTE_API_RES_NONE = 0,
    /** Updates successful */
    APP_LIB_SETTINGS_REMOTE_API_RES_OK = 1,
    /** Updates successful, reset required */
    APP_LIB_SETTINGS_REMOTE_API_RES_OK_RESET = 2,
} app_lib_settings_remote_api_res_e;


/**
 * @brief Structure to hold key management configuration information for the settings API.
 *
 * This structure contains the security key pairs and flags for the key management configuration.
 *
 * @note Key Pair Requirements:
 *       If network_key_pair or management_key_pair is not defined, the key pointer for both
 *       encryption_key_p and authentication_key_p must be NULL. If network_key_pair or management_key_pair is defined,
 *       the key pointer for both encryption_key_p and authentication_key_p must be valid.
 *
 * @note Key Management Configuration:
 *       If flags.app_key_management_configured is set to true (1), the key management feature is enabled, provided that the management_key_pair is set either
 *       by providing valid management_key_pair in this structure or it is already set in the persistent memory.
 *
 * @note Key Management State:
 *       If flags.app_key_management_configured is set to false (0), the key management feature state is not changed, but only provided keys are set. In
 *       other words, the key management feature cannot be disabled without rebooting the device if the key management is enabled.
 *
 * @note Key Revocation Default:
 *       flags.revoke_keys=false is the default value, which should be used in most cases.
 *
 * @note Key Revocation Special Cases:
 *       flags.revoke_keys=true should be used only in special cases where the application needs to revoke the keys. In this case,
 *       other values from this structure are ignored. Note that this operation removes all keys from the persistent storage. The device is
 *       not able to join to the network without the network keys so the application must provide new keys to recover from this situation.
 *       Key revocation will also disable the key management feature until it's activated again with flags.app_key_management_configured=true.
 *
 * @note Device Support:
 *       flags.app_key_management_supported indicates whether app key management is supported by the device.
 *
 * @note Flag Application Behavior:
 *       When apply_flags=0, only key provisioning is performed and no other flags take effect.
 */
typedef struct __attribute__((packed))
{
    struct
    {
        const uint8_t (*encryption_key_p)
            [APP_LIB_SETTINGS_AES_KEY_NUM_BYTES]; /**< Encryption key pointer */
        const uint8_t (*authentication_key_p)
            [APP_LIB_SETTINGS_AES_KEY_NUM_BYTES]; /**< Authentication key
                                                     pointer */
    } network_key_pair, management_key_pair;

    struct
    {
        /** @brief Flag indicating whether to apply the configuration flags.
         *         When set to 1, all configuration flags are applied.
         *         When set to 0, configuration flags are ignored and current system
         *         state is preserved. */
        uint8_t apply_flags : 1;

        /** @brief Flag indicating that app key management is supported.
         *         When apply_flags=0, this flag is ignored and current state preserved. */
        uint8_t app_key_management_supported : 1;

        /** @brief Flag indicating that app key management is configured.
         *         When apply_flags=0, this flag is ignored and current state preserved. */
        uint8_t app_key_management_configured : 1;

        /** @brief Flag to let application force key revocation.
         *         When apply_flags=0, this flag is ignored. */
        uint8_t revoke_keys : 1;

        /** @brief Reserved bits for future use */
        uint8_t reserved : 4;
    } flags;

    uint8_t network_key_pair_seq; /**< Network key pair sequence number, 1-255 */
    uint8_t management_key_pair_seq; /**< Management key pair sequence number for diagnostics */
} app_lib_settings_key_management_configuration_t;


/**
 * @brief Node role type
 *
 * List of possible roles are defined in @ref app_lib_settings_role_e
 */
typedef uint8_t app_lib_settings_role_t;

/**
 * @brief Callback used for determining on which multicast groups the device
 * belongs
 *
 * As an argument, the stack sets the address of the multicast group. If device
 * belongs to that group, callback function returns true. If not, callback
 * returns false.
 *
 * This callback is called when device receives multicast packet. The return
 * value is then determined whether data shall be received by standard means
 * (i.e. data reception callback, see @ref
 * app_lib_data_set_data_received_cb_f "lib_data->setDataReceivedCb")
 *
 * @param   group_addr
 *          Group address (with @ref APP_ADDR_MULTICAST bitmask set)
 * @return  true: is in multicast group, false: is not in multicast group
 * @note    Keep the function execution time moderately short, i.e. do not
 *          execute any time-consuming operations directly in this callback!
 *
 * Usage: see documentation of [lib_settings->registerGroupQuery](@ref app_lib_settings_set_group_query_cb_f).
 */
typedef bool (*app_lib_settings_is_group_cb_f)(app_addr_t group_addr);

/**
 * @brief Reset all settings to default values.
 *
 * - Feature lock bits: not set
 * - Node address: not set
 * - Network address: not set
 * - Network channel: not set
 * - Node role: autorole le
 * - Legacy security keys: not set
 *   - Authentication key: not set
 *   - Encryption key: not set
 * - Scalable key management security keys: not set
 *   - Network encryption/auth keys: not set
 *   - Management encryption/auth keys: not set
 * - Access cycle range: Minimum value according to profile. Max value 8000 ms.
 *
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_STACK_STATE if stack is running
 *          @ref APP_RES_RESOURCE_UNAVAILABLE if reset of secure storage records
 *               failed
 */
typedef app_res_e (*app_lib_settings_reset_all_f)(void);

/**
 * @brief Get feature lock bits
 *
 * Feature lock bits determine which features are
 * permitted at runtime. A cleared bit marks that a feature is locked.
 * Some features are governed by the stack, some checks are implemented on the
 * application side, where applicable. Feature lock bits are active only when
 * feature lock key is set. Feature lock bits are documented in WP-RM-100
 * Wirepas Mesh Dual-MCU API Reference Manual.
 *
 * @param   bits_p
 *          Pointer to store the result
 * @return  Result code, @ref APP_RES_OK if successful,
 *          APP_RES_INVALID_NULL_POINTER if bits_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_feature_lock_bits_f)(
    uint32_t * bits_p);

/**
 * @brief Set feature lock bits
 *
 * See [lib_settings->getFeatureLockBits](@ref app_lib_settings_get_feature_lock_bits_f)() for a description of feature lock bits.
 * A cleared bit marks a feature locked. Reserved bits must remain set.
 *
 * @param   bits
 *          Feature lock bits
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_VALUE if an unsupported lock bit is set to 0
 */
typedef app_res_e (*app_lib_settings_set_feature_lock_bits_f)(uint32_t bits);

/**
 * @brief Check if feature lock key is set
 *
 * If set the feature lock is locked. It is not possible to actually read
 *  the key from the stack. The @p key_p parameter is ignored.
 *
 * @param   key_p
 *          A dummy parameter, reserved for future, set to NULL
 * @return  Result code, @ref APP_RES_OK if a key set,
 *          APP_RES_INVALID_CONFIGURATION if the key is all 0xff, i.e. not set
 * @note    Reading the actual key value is not possible, for security reasons
 */
typedef app_res_e (*app_lib_settings_get_feature_lock_key_f)(uint8_t * key_p);

/**
 * @brief Set feature lock key
 *
 * Lock or unlock the feature lock. @p key_p must
 * point to @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes. The feature lock key
 * is not an AES-128 key, but it is guaranteed to be the same size as an AES-128
 * key.
 *
 * Feature lock key can only be set when the feature lock is unlocked. Unlocking
 * is done by setting the key using the same key as when locking it. A key of
 * all <code>0xff</code> (hex) bytes is considered an unset key. Setting such a
 * key does not lock the feature lock.
 *
 * @param   key_p
 *          Pointer to key, @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_VALUE if a key is set and trying to unlock with
 *          a wrong key, @ref APP_RES_INVALID_NULL_POINTER if @p key_p is NULL
 */
typedef app_res_e (*app_lib_settings_set_feature_lock_key_f)(
    const uint8_t * key_p);

/**
 * @brief Get node address
 *
 * @param   addr_p
 *          Pointer to store the result
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_CONFIGURATION if node address not set,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p addr_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_node_address_f)(app_addr_t * addr_p);

/**
 * @brief Set node address
 *
 * There is no default node address.
 *
 *
 * @param   addr
 *          Own node address to set
 * @return  Result code, @ref APP_RES_OK if successful, @ref
 * APP_RES_INVALID_VALUE if @p addr is invalid, @ref APP_RES_INVALID_STACK_STATE
 * if stack is running
 * @note    Function must be called with a valid node address before the stack
 * can be started.
 */
typedef app_res_e (*app_lib_settings_set_node_address_f)(app_addr_t addr);

/**
 * @brief Get network address
 *
 * @param   addr_p
 *          Pointer to store the result
 * @return  Result code, @ref APP_RES_OK if successful,
 *          APP_RES_INVALID_CONFIGURATION if network address not set,
 *          APP_RES_INVALID_NULL_POINTER if addr_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_network_address_f)(
    app_lib_settings_net_addr_t * addr_p);

/**
 * @brief Set network address
 *
 * There is no default network address.
 *  *
 * @param   addr
 *          Network address to set
 * @return  Result code, @ref APP_RES_OK if successful, APP_RES_INVALID_VALUE
 *          if addr is invalid, APP_RES_INVALID_STACK_STATE if stack is running
 * @note    Function must be called with a valid network address before
 *          the stack can be started
 */
typedef app_res_e (*app_lib_settings_set_network_address_f)(
    app_lib_settings_net_addr_t addr);

/**
 * @brief   Get network channel
 * @param   channel_p
 *          Pointer to store the result
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_CONFIGURATION if network channel not set,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p channel_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_network_channel_f)(
    app_lib_settings_net_channel_t * channel_p);

/**
 * @brief Set network channel
 *
 * Different radio architectures have different number of
 * channels available. Function @ref
 * app_lib_settings_get_network_channel_limits_f
 * "lib_settings->getNetworkChannelLimits"() can be used to determine the
 * minimum and maximum channel number available. There is no default network
 * channel.
 *
 * @param   channel
 *          Network channel to set
 * @return  Result code, @ref APP_RES_OK if successful, @ref
 * APP_RES_INVALID_VALUE if @p channel is invalid, @ref
 * APP_RES_INVALID_STACK_STATE if stack is running
 * @note    Function must be called with a valid network channel before the
 *          stack can be started.
 */
typedef app_res_e (*app_lib_settings_set_network_channel_f)(
    app_lib_settings_net_channel_t channel);

/**
 * Get node role. Utility functions @c app_lib_settings_get_base_role()
 * and @c app_lib_settings_get_flags_role() can be used to split the
 * node value to a base role and role flag bits, respectively.
 *
 * @param   role_p
 *          Pointer to store the result
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_CONFIGURATION if node role not set,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p role_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_node_role_f)(
    app_lib_settings_role_t * role_p);

/**
 * @brief  Set node role
 *
 * Default node role is headnode with the autorole flag set.
 *
 * Code example:
 *
 * @code
 *
 * void App_init(const app_global_functions_t * functions)
 * {
 *     // Configure node as Headnode, low-latency
 *     // This call force the role, and prevent RemoteAPI to change it
 *     lib_settings->setNodeRole(APP_LIB_SETTINGS_ROLE_HEADNODE_LL);
 *
 *     ...
 * }
 * @endcode
 *
 * @param   role
 *          New role
 * @return  Result code, @ref APP_RES_OK if successful, @ref
 * APP_RES_INVALID_VALUE if @p role is invalid, @ref APP_RES_INVALID_STACK_STATE
 * if stack is running
 */
typedef app_res_e (*app_lib_settings_set_node_role_f)(
    app_lib_settings_role_t role);

/**
 * @brief Check if authentication key is set
 *
 * It is not possible to actually read the key from the stack.
 * The @p key_p parameter is ignored.
 *
 * @param   key_p
 *          If NULL, key is not return but return code will inform if keys are
 * set or not. Otherwise, pointer to a ram area of 16 bytes where the key can be
 * copied. It is updated only if return value is APP_RES_OK.
 * @return  Result code, @ref APP_RES_OK if a key set,
 *          APP_RES_INVALID_CONFIGURATION if the key is all 0xff, i.e. not set
 */
typedef app_res_e (*app_lib_settings_get_authentication_key_f)(uint8_t * key_p);

/**
 * @brief  Set authentication key
 *
 * @p key_p must point to @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes.
 * By default, no authentication key is set.
 *
 * A key of all <code>0xff</code> (hex) bytes is considered an unset key.
 * Setting such a key disables encryption and authentication.
 *
 * @param   key_p
 *          Pointer to AES-128 key,
 *          @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_STACK_STATE if stack is running,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p key_p is NULL
 * @note    Note that both the encryption and authentication keys must be set
 *          for the encryption or authentication to be enabled. It is NOT enough
 *          to set just one key.
 */
typedef app_res_e (*app_lib_settings_set_authentication_key_f)(
    const uint8_t * key_p);

/**
 * @brief Check if encryption key is set
 *
 * It is not possible to actually read the key from the stack.
 * The @p key_p parameter is ignored.
 *
 * @param   key_p
 *          If NULL, key is not return but return code will inform if keys are
 * set or not. Otherwise, pointer to a ram area of 16 bytes where the key can be
 * copied. It is updated only if return value is APP_RES_OK.
 * @return  Result code, @ref APP_RES_OK if a key set,
 *          APP_RES_INVALID_CONFIGURATION if the key is all 0xff, i.e. not set
 */
typedef app_res_e (*app_lib_settings_get_encryption_key_f)(uint8_t * key_p);

/**
 * @brief  Set encryption key
 *
 * @p key_p must point to @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes.
 * By default, no encryption key is set.
 *
 * A key of all <code>0xff</code> (hex) bytes is considered an unset key.
 * Setting such a key disables encryption and authentication.
 *
 * @param   key_p
 *          Pointer to AES-128 key,
 *          @ref APP_LIB_SETTINGS_AES_KEY_NUM_BYTES bytes
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_STACK_STATE if stack is running,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p key_p is NULL
 * @note    Note that both the encryption and authentication keys must be set
 *          for the encryption or authentication to be enabled. It is NOT enough
 *          to set just one key.
 */
typedef app_res_e (*app_lib_settings_set_encryption_key_f)(
    const uint8_t * key_p);

/**
 * @brief  Get the access cycle range
 *
 * The values are in milliseconds. This setting is only meaningful for nodes
 * that route data for others, i.e. sinks and headnodes.
 *
 * @param   ac_min_value_p
 *          Pointer to store the minimum current access cycle value
 * @param   ac_max_value_p
 *          Pointer to store the maximum current access cycle value
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_CONFIGURATION if access cycle range not set,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p ac_min_value_p or @p
 *          ac_max_value_p is NULL
 *
 * Example:
 * @code
 * uint16_t min,max;
 * lib_settings->getAcRange(&min, &max);
 * @endcode
 */
typedef app_res_e (*app_lib_settings_get_ac_range_f)(uint16_t * ac_min_value_p,
                                                     uint16_t * ac_max_value_p);

/**
 * @brief   Set range for access cycle
 *
 * Set the access cycle range that this node uses to serve its neighbors. This
 * setting is only meaningful for nodes that route data for others, i.e. sinks
 * and headnodes.
 *
 * By default, the stack automatically selects a suitable access cycle, between
 * 2, 4 or 8 seconds, depending on current network traffic. Setting low access
 * cycle range does not improve latency, however pining it to a higher value
 * can reduce power consumption despite reducing the throughput.
 *
 * The values are in milliseconds. Function @ref
 * app_lib_settings_get_ac_range_limits_f "lib_settings->getAcRangeLimits"()
 * can also be used to query the limits.
 * Default range is min. 2000 ms, max. 8000 ms.
 *
 * Valid values are:
 * <table>
 * <tr><th>Value<th>Description
 * <tr><td>2000<td>2 seconds
 * <tr><td>4000<td>4 seconds
 * <tr><td>8000<td>8 seconds
 * </table>
 *
 * If value is not set, or maximum > minimum, the stack selects an appropriate
 * access cycle based on current network traffic. If maximum = minimum,
 * the user can force the access cycle to a specific duration.
 * Range is not set by default. Only a factory reset can restore range back
 * to the unset state.
 *
 * Example:
 * @code
 * // Reduce throughput by 1/4 to lower power consumption
 * lib_settings->setAcRange(8000, 8000);
 * @endcode
 *
 * @param   ac_min_value
 *          Minimum access cycle value to set
 * @param   ac_max_value
 *          Maximum access cycle value to set
 * @return  Result code, @ref APP_RES_OK if successful, @ref
 *          APP_RES_INVALID_VALUE if @p ac_min_value or @p ac_max_value is
 *          invalid
 *
 * @note    This setting is not possible when device role has flag
 *          @c APP_LIB_SETTINGS_ROLE_FLAG_LL mode set. Instead, those devices
 *          always have automatic access cycle selection enabled.
 */
typedef app_res_e (*app_lib_settings_set_ac_range_f)(uint16_t ac_min_value,
                                                     uint16_t ac_max_value);

/**
 * @brief Get the maximum offline scan interval in seconds
 *
 * The maximum offline scan interval determines the maximum interval between two
 * scans for neighbors when device has no route to a sink.
 *
 * @param   max_scan_p
 *          Pointer to store the scanning interval value
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_STACK_STATE if stack is not running,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p max_scan_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_offline_scan_f)(uint16_t * max_scan_p);

/**
 * @brief  Set the maximum offline scan interval in seconds
 *
 * The maximum offline scan interval determines how often a node scans for
 * neighbors when it has no route to a sink.
 * Value is automatically limited to a valid range. The default value,
 * before calling [lib_settings->setOfflineScan](@ref app_lib_settings_set_offline_scan_f)() is 600 seconds (10 minutes) for Low Energy
 * Mode and 30 seconds for Low Latency Mode.
 *
 * Valid offline scan values:
 * <table>
 * <tr><th>Value<th>Description
 * <tr><td>20<td>Minimum: 20 seconds
 * <tr><td>600<td>Maximum: 3600 seconds (1 hour)
 * </table>
 *
 * To manually start a neighbor scan, function startScanNbors() in the State
 * library ( @ref wms_state.h) can be used.
 *
 * @param   max_scan
 *          Minimum maximum scanning interval value
 * @return  Result code, @ref APP_RES_OK if successful, @ref
 *          APP_RES_INVALID_VALUE if @p max_scan is invalid
 */
typedef app_res_e (*app_lib_settings_set_offline_scan_f)(uint16_t max_scan);

/**
 * @brief   Get network channel range
 *
 * Return the minimum and maximum network channel value that can be used when
 * setting the network channel with the @ref
 * app_lib_settings_set_network_channel_f "lib_settings->setNetworkChannel"()
 * function
 *
 * @param   min_value_p
 *          Pointer to store the minimum network channel value allowed
 * @param   max_value_p
 *          Pointer to store the maximum network channel value allowed
 * @return  Result code, @ref APP_RES_OK if successful,
 *          APP_RES_INVALID_NULL_POINTER if min_value_p or max_value_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_network_channel_limits_f)(
    uint16_t * min_value_p, uint16_t * max_value_p);

/**
 * @brief   Get access cycle range limits
 *
 * Return the minimum and maximum for valid access cycle range, in milliseconds,
 * that can be used when setting the access cycle range with the @ref
 * app_lib_settings_set_ac_range_f "lib_settings->setAcRange"() function.
 *
 * @param   min_value_p
 *          Pointer to store the minimum access cycle value allowed
 * @param   max_value_p
 *          Pointer to store the maximum access cycle value allowed
 * @return  Result code, @ref APP_RES_OK if successful,
 *          APP_RES_INVALID_NULL_POINTER if min_value_p or max_value_p is NULL
 */
typedef app_res_e (*app_lib_settings_get_ac_range_limits_f)(
    uint16_t * min_value_p, uint16_t * max_value_p);

/**
 * @brief  Set the callback function for multicat groups
 *
 * The callback is called when stack needs to determine on which multicast
 * groups the device belongs to. If callback is not defined, device does not
 * belong to any multicast groups.
 *
 * @param   cb
 *          The function to be executed, or NULL to unset
 * @return  Result code, always @ref APP_RES_OK
 *
 * Example on use:
 *   @code
 *   // This device belongs to this group
 *   #define OWN_GROUP (APP_ADDR_MULTICAST | 1)
 *
 *   bool group_query_cb(app_addr_t group_addr)
 *   {
 *       return (group_addr == OWN_GROUP);
 *   }
 *
 *   void App_init(const app_global_functions_t * functions)
 *   {
 *       lib_settings->registerGroupQuery(group_query_cb);
 *       // ...
 *       lib_state->startStack();
 *   }
 *   @endcode
 */
typedef app_res_e (*app_lib_settings_set_group_query_cb_f)(
    app_lib_settings_is_group_cb_f cb);

/**
 * @brief Get reserved channels
 *
 * Get a bit array of reserved channels, or channels that are marked to be
 * avoided by the Wirepas Mesh protocol. Each set bit marks a channel that is
 * to be avoided. The least significant bit of the first byte is channel 1, the
 * least significant bit of the next byte is channel 9 and so forth.
 *
 * @param   channels_p
 *          Pointer to store the reserved channels bit array
 *          Each set bit marks the channel as reserved
 *          LSb of first byte is channel 1, MSb of first byte is channel 8,
 *          LSb of second byte is channel 9, an so on
 *          etc
 * @param   num_bytes
 *          Number of bytes pointed by @p channels_p
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p channels_p is NULL,
 *          @ref APP_RES_INVALID_VALUE if last reserved channel does not fit in
 *          @p num_bytes
 * @note    @p channels_p bit array can be longer than the maximum
 *          reserved channel. Remaining channels are marked as not reserved
 */
typedef app_res_e (*app_lib_settings_get_reserved_channels_f)(
    uint8_t * channels_p, size_t num_bytes);

/**
 * @brief Set reserved channels
 *
 * Mark channels as reserved, or to be avoided by the Wirepas Mesh protocol.
 * Each set bit marks a channel that is to be avoided. The least significant bit
 * of the first byte is channel 1, the least significant bit of the next byte is
 * channel 9 and so forth. The @p channels_p bit array may be shorter than the
 * number of channels. In that case, the remaining channels are marked as not
 * reserved. The bit array may be longer too, provided that the highest bit set
 * in it corresponds to a valid channel number (see section 3.6.4.27), i.e.
 * extra zeros are ignored.
 *
 * A node may still transmit on a reserved channel if it has a neighbor that has
 * not been configured to avoid the channel. For best results, all nodes in a
 * network should be configured to have the same reserved channels. Reserving
 * the network channel will result in undefined behavior.
 *
 * The reserved channels array is not stored in permanent memory.
 *
 * @param   channels
 *          Pointer to bit array to load the reserved channels
 *          Each set bit marks the channel as reserved
 *          LSb of first byte is channel 1, MSb of first byte is channel 8,
 *          LSb of second byte is channel 9, an so on
 * @param   num_bytes
 *          Number of bytes pointed by @p channels_p
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if @p channels_p is NULL,
 *          @ref APP_RES_INVALID_VALUE if a bit in @p channels_p is set for a
 *          channel larger than the maximum channel number
 * @note    @p channels_p bit array can be shorter than the maximum number of
 *          channels. Remaining channels are marked as not reserved
 * @note    In the current implementation, reserved channels are not stored
 *          in persistent memory. Application must call setReservedChannels()
 *          in App_init()
 */
typedef app_res_e (*app_lib_settings_set_reserved_channels_f)(
    const uint8_t * channels_p, size_t num_bytes);

/**
 * @brief   Check that the network address is valid.
 * @param   addr
 *          The network address to check.
 * @return  True if network address is valid.
 */
typedef bool (*app_lib_settings_is_valid_network_address_f)(
    app_lib_settings_net_addr_t addr);

/**
 * @brief   Check that the network channel is valid.
 * @param   channel
 *          The network channel to check.
 * @return  True if network channel is valid.
 */
typedef bool (*app_lib_settings_is_valid_network_channel_f)(uint8_t channel);

/**
 * @brief   Check that the node address is valid.
 * @param   addr
 *          The node address to check.
 * @return  True if node address is valid.
 */
typedef bool (*app_lib_settings_is_valid_node_address_f)(app_addr_t addr);

/**
 * @brief   Check that the node role is valid.
 * @param   role
 *          The node role to check.
 * @return  True if node role is valid.
 */
typedef bool (*app_lib_settings_is_valid_node_role_f)(
    app_lib_settings_role_t role);

/**
 * @brief Key management configuration using the settings API.
 *
 * This function configures key management using the provided key configuration
 * information.
 *
 * @param[in] key_mgmt_cfg_p Pointer to the key management information
 * structure.
 *
 * @return Result code, @ref APP_RES_OK if successful,
 *         @ref APP_RES_INVALID_NULL_POINTER if key_info is NULL,
 *         or other error codes based on the provisioning result.
 */
typedef app_res_e (*app_lib_settings_key_management_config_f)(
    const app_lib_settings_key_management_configuration_t * key_mgmt_cfg_p);


/** Bit value indicating that the CSAP Write request was sent in a unicast data packet */
#define REMOTE_API_CSAP_WRITE_UNICAST 0x1

// Return codes, these must match the values of the enum response_type_e in remote API
/** A special return value: normal response should be generated */
#define REMOTE_API_CSAP_RESPONSE_NORMAL 0
/** A special return value: no response needs to be generated */
#define REMOTE_API_CSAP_RESPONSE_NONE 1
/** Failure: Access is denied, i.e. feature lock key is set */
#define REMOTE_API_CSAP_RESPONSE_ACCESS_DENIED 0xf8
/** Failure: Trying to read an attribute that is write-only */
#define REMOTE_API_CSAP_RESPONSE_WRITE_ONLY_ATTRIBUTE 0xf9
/** Failure: Request is only valid for unicast, invalid when broadcasted */
#define REMOTE_API_CSAP_RESPONSE_INVALID_BROADCAST_REQUEST 0xfa
/** Failure: No begin request issued, or update delay countdown running */
#define REMOTE_API_CSAP_RESPONSE_INVALID_BEGIN 0xfb
/** Failure: No space in response, cannot carry out request */
#define REMOTE_API_CSAP_RESPONSE_NO_SPACE_FOR_RESPONSE 0xfc
/** Failure: A value in the request is invalid or value is not set */
#define REMOTE_API_CSAP_RESPONSE_INVALID_VALUE 0xfd
/** Failure: Request has too many or too few parameters */
#define REMOTE_API_CSAP_RESPONSE_INVALID_LENGTH 0xfe
/** Failure: Request was not recognized */
#define REMOTE_API_CSAP_RESPONSE_UNKNOWN_REQUEST 0xff

/**
 * @brief   Struct containing the CSAP Read adn Write request and response
 *          information. Needed the requests are handled on application
 *          side.
 */
typedef struct
{
    /** Pointer to buffer where data can be read from.*/
    const uint8_t * request_buffer;
    /** Pointer to buffer where data can be written to.*/
    uint8_t * response_buffer;
    /** Amount of bytes that can be read from the request_buffer.*/
    uint8_t request_buffer_len;
    /**
     * Caller sets this to amount of bytes that can be written to the
     * response_buffer. Callee sets this to amount of bytes that was
     * written to the response_buffer.
     */
    uint8_t response_buffer_len;
} app_lib_settings_remote_api_csap_data_t;

/**
 * @brief           A callback function type used for providing the Remote API
 *                  CSAP Write request information to application side and
 *                  the response information back to Remote API on stack side.
 *                  Note that the data can be cached into a temporary location
 *                  until a call of the callback registered with
 *                  lib_settings->registerRemoteApiCsapUpdateCb is received.
 *                  This may be used to maintain data integrity if multiple
 *                  CSAP Write attribute ID values has to come in action at
 *                  the same time
 * @param           attr_id
 *                  Attribute ID value recorded in the CSAP Write request.
 * @param           flags
 *                  Additional flags. Bit 0: indicates if the Remote API data
 *                  packet containing the CSAP Write Request was sent as
 *                  unicast.
 * @param[in,out]   data
 *                  Pointer to a data structure that contains buffers for
 *                  request and response data. Buffers are preallocated and the
 *                  number of bytes that can be read and written, given in the
 *                   *_buffer_length members, must be strictly obeyed.
 * @return          REMOTE_API_CSAP_RESPONSE_NORMAL shall be returned if request
 *                  was read and response was written successfully,
 *                  One of the REMOTE_API_CSAP_RESPONSE_* error codes shall be
 *                  returned if the request was of unexpected length, contents
 *                  was invalid or the response_buffer_len was too small to fit
 *                  the whole response, etc.
 */
typedef uint8_t (*app_lib_settings_remote_api_csap_write_f)( uint16_t attr_id, uint32_t flags, app_lib_settings_remote_api_csap_data_t * data);

/**
 * @brief   Register a callback function which is to be called when a CSAP
 *          Write request is received and it needs to be handled on application
 *          side.
 *
 * @param   cb
 *          A callback that handles a received Remote API CSAP Write request.
 *          See more details about the callback in
 *          @ref app_lib_settings_remote_api_csap_write_f.
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if cb is NULL
 */
typedef app_res_e (*app_lib_settings_register_remote_api_csap_write_cb_f)(
    app_lib_settings_remote_api_csap_write_f cb);


/**
 * @brief           A callback function type used for providing the Remote API
 *                  CSAP Read request information to application side and
 *                  the response information back to Remote API on stack side.
 * @param           attr_id
 *                  Attribute ID value recorded in the Remote API CSAP Read
 *                  request.
 * @param[in,out]   data
 *                  Pointer to a data structure that contains buffers for
 *                  request and response data. Buffers are preallocated and the
 *                  number of bytes that can be read and written, given in the
 *                   *_buffer_length members, must be strictly obeyed.
 *                  Commonly the request_buffer_len is zero and the attribute
 *                  ID uniquely determines the response given.
 * @return          REMOTE_API_CSAP_RESPONSE_NORMAL shall be returned if the
 *                  request was read and response was written successfully,
 *                  One of the REMOTE_API_CSAP_RESPONSE_* error codes shall
 *                  be returned if the attribute ID was unexpected or the
 *                  response_buffer_len was too small to fit the whole
 *                  response.
 */
typedef uint8_t (*app_lib_settings_remote_api_csap_read_f)( uint16_t attr_id, app_lib_settings_remote_api_csap_data_t * data);


/**
 * @brief   Register a callback function which is to be called when a CSAP
 *          Read request is received and it needs to be handled on
 *          application side.
 * @param   cb
 *          A callback that handles a received Remote API CSAP Read request.
 *          See more details about the callback in
 *          @ref app_lib_settings_remote_api_csap_read_f.
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if cb is NULL
 */
typedef app_res_e (*app_lib_settings_register_remote_api_csap_read_cb_f)(
    app_lib_settings_remote_api_csap_read_f cb);


/**
 * @brief           A callback function type used for providing the Remote API
 *                  Update request information to application side. There is no
 *                  need to respond the Update request on application side,
 *                  thus no function arguments. Application may (optionally)
 *                  cache the data received from CSAP Write Requests on
 *                  application side and put them into action at the same time
 *                  once this callback is called.
 */
typedef app_lib_settings_remote_api_res_e (*app_lib_settings_remote_api_csap_update_f)( void );


/**
 * @brief   Register a callback function which is to be called when a CSAP
 *          Update request is received and it needs to be handled on
 *          application side.
 * @param   cb
 *          A callback that handles a received Remote API CSAP Update request.
 *          See more details about the callback in
 *          @ref app_lib_settings_remote_api_csap_update_f.
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if cb is NULL
 */
typedef app_res_e (*app_lib_settings_register_remote_api_csap_update_cb_f)(
    app_lib_settings_remote_api_csap_update_f cb);


/**
 * @brief           A callback function type used for providing the Remote API
 *                  Cancel request information to application side. In case the
 *                  application has cached the data received from CSAP Write
 *                  Requests, the cached data shall be cleared in a call to
 *                  this function. .There is no need to respond the Cancel
 *                  request on application side, thus no function arguments.
 */
typedef void (*app_lib_settings_remote_api_csap_cancel_f)( void );

/**
 * @brief   Register a callback function which is to be called when a CSAP
 *          Cancel request is received and it needs to be handled on
 *          application side.
 * @param   cb
 *          A callback that handles a received Remote API CSAP Cancel request.
 *          See more details about the callback in
 *          @ref app_lib_settings_remote_api_csap_cancel_f.
 * @return  Result code, @ref APP_RES_OK if successful,
 *          @ref APP_RES_INVALID_NULL_POINTER if cb is NULL
 */
typedef app_res_e (*app_lib_settings_register_remote_api_csap_cancel_cb_f)(
    app_lib_settings_remote_api_csap_cancel_f cb);

/**
 * @brief   Settings library API
 *
 * This structure contains function pointers to all the settings library
 * functions. The library is used to configure various network and node
 * parameters.
 */
typedef struct
{
    app_lib_settings_reset_all_f                  resetAll;
    app_lib_settings_get_feature_lock_bits_f      getFeatureLockBits;
    app_lib_settings_set_feature_lock_bits_f      setFeatureLockBits;
    app_lib_settings_get_feature_lock_key_f       getFeatureLockKey;
    app_lib_settings_set_feature_lock_key_f       setFeatureLockKey;
    app_lib_settings_get_node_address_f           getNodeAddress;
    app_lib_settings_set_node_address_f           setNodeAddress;
    app_lib_settings_get_network_address_f        getNetworkAddress;
    app_lib_settings_set_network_address_f        setNetworkAddress;
    app_lib_settings_get_network_channel_f        getNetworkChannel;
    app_lib_settings_set_network_channel_f        setNetworkChannel;
    app_lib_settings_get_node_role_f              getNodeRole;
    app_lib_settings_set_node_role_f              setNodeRole;
    app_lib_settings_get_authentication_key_f     getAuthenticationKey;
    app_lib_settings_set_authentication_key_f     setAuthenticationKey;
    app_lib_settings_get_encryption_key_f         getEncryptionKey;
    app_lib_settings_set_encryption_key_f         setEncryptionKey;
    app_lib_settings_get_ac_range_f               getAcRange;
    app_lib_settings_set_ac_range_f               setAcRange;
    app_lib_settings_get_offline_scan_f           getOfflineScan;
    app_lib_settings_set_offline_scan_f           setOfflineScan;
    app_lib_settings_get_network_channel_limits_f getNetworkChannelLimits;
    app_lib_settings_get_ac_range_limits_f        getAcRangeLimits;
    app_lib_settings_set_group_query_cb_f         registerGroupQuery;
    app_lib_settings_get_reserved_channels_f      getReservedChannels;
    app_lib_settings_set_reserved_channels_f      setReservedChannels;
    app_lib_settings_is_valid_network_address_f   isValidNetworkAddress;
    app_lib_settings_is_valid_network_channel_f   isValidNetworkChannel;
    app_lib_settings_is_valid_node_address_f      isValidNodeAddress;
    app_lib_settings_is_valid_node_role_f         isValidNodeRole;
    /** Configure key management*/
    app_lib_settings_key_management_config_f      keyManagementConfiguration;
    /** Register a callback that is called when a Remote API CSAP Write request is received*/
    app_lib_settings_register_remote_api_csap_write_cb_f  registerRemoteApiCsapWriteCb;
    /** Register a callback that is called when a Remote API CSAP Read request is received*/
    app_lib_settings_register_remote_api_csap_read_cb_f  registerRemoteApiCsapReadCb;
    /** Register a callback that is called when a Remote API Update request is received*/
    app_lib_settings_register_remote_api_csap_update_cb_f  registerRemoteApiCsapUpdateCb;
    /** Register a callback that is called when a Remote API Cancel request is received*/
    app_lib_settings_register_remote_api_csap_cancel_cb_f  registerRemoteApiCsapCancelCb;
} app_lib_settings_t;

#endif /* APP_LIB_SETTINGS_H_ */
