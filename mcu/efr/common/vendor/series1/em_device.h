/*
 * em_device.h
 *
 *  Chip variant include file selection.
 */

#ifndef EM_DEVICE_H_
#define EM_DEVICE_H_

#if defined(EFR32FG12)
#include "efr32fg12/Include/em_device.h"
#define GPIO_PORT_MAX 10
#elif defined(EFR32FG13)
#include "efr32fg13/Include/em_device.h"
#define GPIO_PORT_MAX 5
#else
#error "em_device.h: Unknown EFR32 SERIES1 PART"
#endif

#ifndef __SYSTEM_CLOCK
#define __SYSTEM_CLOCK                 (38400000UL)
#endif

#endif /* EM_DEVICE_H_ */
