# Copyright 2026 Wirepas Ltd. All Rights Reserved.
#
# See file LICENSE.txt for full license details.

#
# EUSART Multi-Instance Driver Makefile
#
# This makefile builds the platform-agnostic EUSART driver and includes
# platform-specific hardware configuration files.
#

# Define directories
EUSART_MULTI_DIR := $(EFR32_HAL_PREFIX)usart/series2/eusart_multi
EUSART_LDMA_DIR := $(EFR32_HAL_PREFIX)usart/series2/ldma

# UART configuration from config.mk (lowercase) -> CFLAGS (uppercase)
# User must define usart_multi_max_instances in config.mk
ifneq ($(usart_multi_max_instances),)
    CFLAGS += -DUSART_MULTI_MAX_INSTANCES=$(usart_multi_max_instances)
endif

ifneq ($(usart_multi_tx_buffer_size),)
    CFLAGS += -DUSART_MULTI_TX_BUFFER_SIZE=$(usart_multi_tx_buffer_size)
endif

ifneq ($(usart_multi_rx_buffer_size),)
    CFLAGS += -DUSART_MULTI_RX_BUFFER_SIZE=$(usart_multi_rx_buffer_size)
endif

# Generic source files (platform-independent)
EUSART_GENERIC_SRCS := \
    $(EUSART_MULTI_DIR)/eusart_multi.c \
    $(EUSART_MULTI_DIR)/eusart_multi_ints.c \
    $(EUSART_MULTI_DIR)/eusart_multi_ldma.c \
    $(EUSART_LDMA_DIR)/ldma.c

# Add generic sources
SRCS += $(EUSART_GENERIC_SRCS)

# Add Silicon Labs SDK EUSART and LDMA libraries
SRCS += $(EFR32_VENDOR_PREFIX)em_eusart.c \
        $(EFR32_VENDOR_PREFIX)em_ldma.c

# Add system clock library (if not already added)
# HAL_SYSTEM_C is defined in parent makefile (typically em_cmu.c or em_system.c)
SRCS += $(EFR32_VENDOR_PREFIX)$(HAL_SYSTEM_C)

# Include platform-specific makefile
ifeq ($(MCU_SUB), xg23)
    include $(EUSART_MULTI_DIR)/xg23/makefile

else ifeq ($(MCU_SUB), xg24)
    $(error EUSART multi-instance driver: xg24 support not yet implemented)

else
    $(error EUSART multi-instance driver does not support $(MCU_SUB))

endif

# Backward compatibility wrapper for legacy usart.h API
# Included by default; set usart_exclude_legacy_api=yes to exclude
ifneq ($(usart_exclude_legacy_api), yes)
SRCS += $(MCU_COMMON_SRCS_PATH)hal/uart/usart_compat.c
endif

# Add include paths
INCLUDES += -I$(EUSART_MULTI_DIR) \
            -I$(EUSART_LDMA_DIR)
