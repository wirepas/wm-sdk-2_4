/* Copyright 2020 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef HARDWARE_H_
#define HARDWARE_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(NRF91_PLATFORM)
/** \brief  Platform specific descriptions for nRF91. */
typedef struct
{
    /** Pointer to platform specific modem initialization AT commands.
     *  AT commands are separated from each other with null character ('\0'),
     *  the end of the list is indicated with double null characters ("\0\0").
     *  (introduced in bootloader v9).
     */
    const char * at_commands;
} platform_nrf91_t;
#elif defined(NRF54_PLATFORM)
/** \brief  Platform specific descriptions for nRF54. */
typedef struct
{
    /**
     * Desired capacitor value in fF (femtofarads) for HFXO internal capacitors.
     *
     * The value must be in range [4000, 17000] and adjusted in steps of 250 fF.
     * If the value is zero, internal capacitors are disabled.
     */
    const uint16_t hfxo_int_cap_ff;

    /**
     * Desired capacitor value in fF (femtofarads) for LFXO internal capacitors.
     *
     * The value must be in range [4000, 18000] and adjusted in steps of 500 fF.
     * If the value is zero, internal capacitors are disabled.
     */
    const uint16_t lfxo_int_cap_ff;

    /**
     * Override value for FICR_INFO_PART information.
     *
     * To be used when nRF54L15 HW is used, but device should
     * act as much as possible like nRF54L10 or nRF54L05.
     */
    const uint32_t override_ficr_info_part;
} platform_nrf54_t;
#elif defined(EFR32_PLATFORM)
#include "em_cmu.h"

#if (_SILICON_LABS_32B_SERIES == 1)

/** \brief  Platform specific descriptions for EFR32 Series 1 */
typedef struct
{
    /** Pointer to platform specific HFXO crystal description
     *  (introduced in bootloader v8).
     */
    const CMU_HFXOInit_TypeDef * const hfxoInit;
    /** Pointer to platform specific LFXO crystal description
     *  (introduced in bootloader v8).
     */
    const CMU_LFXOInit_TypeDef * const lfxoInit;
} platform_efr32_t;

#elif (_SILICON_LABS_32B_SERIES == 2)

/** Starting from GSDK 4.1.0 and from beginning of Simplicity SDK Silabs has
 *  decided that enum is not safe as the storage size differs and has changed
 *  all enum typedefinitions of CMU_HFXOInit_TypeDef to be uint32_t although
 *  uint8_t would have been sufficient and compatible for our use. We will
 *  always use old structure with old enum size definition for these devices.
 *  That is the easiest way to ensure compatibility with old bootloader.
 */
typedef struct {
    uint8_t   timeoutCbLsb;            /**< Core bias change timeout. */
    uint8_t   timeoutSteadyFirstLock;  /**< Steady state timeout duration for first lock. */
    uint8_t   timeoutSteady;           /**< Steady state timeout duration. */
    uint8_t   ctuneXoStartup;          /**< XO pin startup tuning capacitance. */
    uint8_t   ctuneXiStartup;          /**< XI pin startup tuning capacitance. */
    uint8_t   coreBiasStartup;         /**< Core bias startup current. */
    uint8_t   imCoreBiasStartup;       /**< Core bias intermediate startup current. */
    uint8_t   coreDegenAna;            /**< Core degeneration control. */
    uint8_t   ctuneFixAna;             /**< Fixed tuning capacitance on XI/XO. */
    uint8_t   ctuneXoAna;              /**< Tuning capacitance on XO. */
    uint8_t   ctuneXiAna;              /**< Tuning capacitance on XI. */
    uint8_t   coreBiasAna;             /**< Core bias current. */
    bool      enXiDcBiasAna;           /**< Enable XI internal DC bias. */
    uint8_t   mode;                    /**< Oscillator mode. */
    bool      forceXo2GndAna;          /**< Force XO pin to ground. */
    bool      forceXi2GndAna;          /**< Force XI pin to ground. */
    bool      disOnDemand;             /**< Disable on-demand requests. */
    bool      forceEn;                 /**< Force oscillator enable. */
#if defined(HFXO_CTRL_EM23ONDEMAND)
    bool      em23OnDemand;            /**< Enable deep sleep. */
#endif
    bool      regLock;                 /**< Lock register access. */
} Legacy_CMU_HFXOInit_TypeDef;

#if defined(HFXO_CTRL_EM23ONDEMAND)
    _Static_assert((sizeof(Legacy_CMU_HFXOInit_TypeDef) == 20), "sizeof Legacy HFXO Init is not 20");
#else
    _Static_assert((sizeof(Legacy_CMU_HFXOInit_TypeDef) == 19), "sizeof Legacy HFXO Init is not 19");
#endif

/** Starting from GSDK 4.1.0 and from beginning of Simplicity SDK Silabs has
 *  decided that enum is not safe as the storage size differs and has changed
 *  all enum typedefinitions of CMU_LFXOInit_TypeDef to be uint32_t although
 *  uint8_t would have been sufficient and compatible for our use. We will
 *  always use old structure with old enum size definition for these devices.
 *  That is the easiest way to ensure compatibility with old bootloader.
 */
typedef struct {
    uint8_t   gain;                    /**< Startup gain. */
    uint8_t   capTune;                 /**< Internal capacitance tuning. */
    uint8_t   timeout;                 /**< Startup delay. */
    uint8_t   mode;                    /**< Oscillator mode. */
    bool      highAmplitudeEn;         /**< High amplitude enable. */
    bool      agcEn;                   /**< AGC enable. */
    bool      failDetEM4WUEn;          /**< EM4 wakeup on failure enable. */
    bool      failDetEn;               /**< Oscillator failure detection enable. */
    bool      disOnDemand;             /**< Disable on-demand requests. */
    bool      forceEn;                 /**< Force oscillator enable. */
    bool      regLock;                 /**< Lock register access. */
} Legacy_CMU_LFXOInit_TypeDef;

_Static_assert((sizeof(Legacy_CMU_LFXOInit_TypeDef) == 11), "sizeof Legacy LFXO Init is not 11");

/** \brief  Platform specific descriptions for EFR32 Series 2 */
typedef struct
{
    /** Pointer to platform specific HFXO crystal description
     *  (introduced in bootloader v8).
     */
    const Legacy_CMU_HFXOInit_TypeDef * const hfxoInit;
    /** Pointer to platform specific LFXO crystal description
     *  (introduced in bootloader v8).
     */
    const Legacy_CMU_LFXOInit_TypeDef * const lfxoInit;
} platform_efr32_t;
#endif // _SILICON_LABS_32B_SERIES
#elif defined(CC13X4_PLATFORM)
/** \brief  Platform specific descriptions for CC13x4 */
typedef struct
{
    /** capArrDelta value to be used with
     *  OSC_AdjustXoscHfCapArray function
     *  (introduced in bootloader v14).
     *  If value is INT8_MAX, adjustment
     *  is not performed.
     */
    const int8_t capArrDelta;
} platform_cc13x4_t;
#endif

/** \brief  Platform specific descriptions. */
typedef union
{
#if defined(NRF52_PLATFORM)
    /** Platform specific descriptions for nRF52.
     *  (dummy, introduced in bootloader v8).
     */
    const void * nrf52;
#elif defined(NRF54_PLATFORM)
    /** Platform specific descriptions for nRF54.
     *  (introduced in bootloader v10).
     */
    const platform_nrf54_t * nrf54;
#elif defined(NRF91_PLATFORM)
    /** Platform specific descriptions for nRF91.
     *  (dummy, introduced in bootloader v8).
     */
    const void * nrf91;
#elif defined(EFR32_PLATFORM)
    /** Platform specific descriptions for EFR32.
     *  (introduced in bootloader v8).
     */
    const platform_efr32_t * efr32;
#elif defined(CC13X4_PLATFORM)
    /** Platform specific descriptions for cc13x4.
     *  (introduced in bootloader v14).
     */
    const platform_cc13x4_t * cc13x4;
#endif
} platform_t;

/** \brief  Hardware features that can be installed on a board. */
typedef struct
{
    /** True if 32kHz crystal is present; default:true
     *  (introduced in bootloader v7).
     */
    const bool crystal_32k;
    /** True if DCDC converter is enabled; default:true
     * (introduced in bootloader v7).
     */
    const bool dcdc;
    /** Platform specific descriptions
     *  (introduced in bootloader v8).
     */
    const platform_t platform;
} hardware_capabilities_t;

/**
 * \brief   Returns board hardware capabilities.
 * \return  Return a structure \ref bl_hardware_capabilities_t with
 *          hardware features installed on the board.
 * \note    There is no need to define this function, it is integrated in the
 *          SDK and is controlled from each board/<board_name>/config.mk.
 */
const hardware_capabilities_t * hardware_getCapabilities(void);

#endif //HARDWARE_H_
