/* Copyright 2025 Wirepas Ltd. All Rights Reserved.
 *
 * See file LICENSE.txt for full license details.
 *
 */

#ifndef MCU_H_
#define MCU_H_

#include "DeviceFamily.h"

/** Macro for constructing a path to TI vendor files within SDK */
#define DeviceFamily_constructPath_SDK(x) <DeviceFamily_DIRECTORY/x>

#include DeviceFamily_constructPath_SDK(cmsis/cc26x4.h)
#include DeviceFamily_constructPath_SDK(inc/hw_sysctl.h)

#include "core_cm33.h"

#endif  // MCU_H_
